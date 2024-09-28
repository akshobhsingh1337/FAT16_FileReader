#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

// Define the structure for Boot Sector and BIOS Parameter Block
typedef struct __attribute__((__packed__))
{
  uint8_t BS_jmpBoot[3];
  uint8_t BS_OEMName[8];
  uint16_t BPB_BytsPerSec;
  uint8_t BPB_SecPerClus;
  uint16_t BPB_RsvdSecCnt;
  uint8_t BPB_NumFATs;
  uint16_t BPB_RootEntCnt;
  uint16_t BPB_TotSec16;
  uint8_t BPB_Media;
  uint16_t BPB_FATSz16;
  uint16_t BPB_SecPerTrk;
  uint16_t BPB_NumHeads;
  uint32_t BPB_HiddSec;
  uint32_t BPB_TotSec32;
  uint8_t BS_DrvNum;
  uint8_t BS_Reserved1;
  uint8_t BS_BootSig;
  uint32_t BS_VolID;
  uint8_t BS_VolLab[11];
  uint8_t BS_FilSysType[8];
} BootSector;

// Define the structure for Directory Entry
typedef struct __attribute__((__packed__))
{
  uint8_t DIR_Name[11];
  uint8_t DIR_Attr;
  uint8_t DIR_NTRes;
  uint8_t DIR_CrtTimeTenth;
  uint16_t DIR_CrtTime;
  uint16_t DIR_CrtDate;
  uint16_t DIR_LstAccDate;
  uint16_t DIR_FstClusHI;
  uint16_t DIR_WrtTime;
  uint16_t DIR_WrtDate;
  uint16_t DIR_FstClusLO;
  uint32_t DIR_FileSize;
} DirectoryEntry;

typedef struct __attribute__((__packed__))
{
  char DIR_Name[256];
  char shortDIR_Name[11];
  char DIR_Attr;
  char decodedAttributes[6];
  uint16_t DIR_FstClusLO;
  uint32_t DIR_FileSize;
  int hour;
  int minute;
  int second;
  int day;
  int month;
  int year;
} FullDirectoryEntry;

typedef struct __attribute__((__packed__))
{
  int openedFile;          // File descriptor
  BootSector *bootEntries; // Boot Sector information
  uint16_t *fatEntries;    // Pointer to FAT entries
  size_t fatSize;          // Size of FAT in bytes
  off_t dataAreaStart;     // Start of the data area
} Volume;

// Define the structure for a file
typedef struct __attribute__((__packed__))
{
  int openedFile;             // File descriptor
  BootSector *bootEntries;    // Boot Sector information
  uint16_t *fatEntries;       // Pointer to FAT entries
  size_t fatSize;             // Size of FAT in bytes
  off_t dataAreaStart;        // Start of the data area
  off_t currentClusterOffset; // Offset within the current cluster
  size_t currentCluster;      // Current cluster number
  size_t *clusterArray;       // Array to store clusters
  size_t clusterArraySize;    // Size of the cluster array
  uint32_t fileSize;          // Size of the file being opened
} File;

// Structure for a long directory entry
typedef struct __attribute__((__packed__))
{
  uint8_t LDIR_Ord;
  uint8_t LDIR_Name1[10];
  uint8_t LDIR_Attr;
  uint8_t LDIR_Type;
  uint8_t LDIR_Chksum;
  uint8_t LDIR_Name2[12];
  uint16_t LDIR_FstClusLO;
  uint8_t LDIR_Name3[4];
} LongDirectoryEntry;

// Union to merge a short directory and long directory entry
typedef union
{
  DirectoryEntry dirEntry;
  LongDirectoryEntry longDirEntry;
} EntryUnion;

// Constant file headers for printing
const char *fileDetailsFormat = "%-14u   %02d:%02d:%02d             %02d/%02d/%-4d           %c%c%c%c%c%c       %-10u    %s\n";
const char *headerFormat = "%-14s   %-15s   %-12s   %-9s   %-8s      %-s\n";

// Function to read a sector from the file
ssize_t readSector(int openedFile, void *buffer, off_t offset, size_t sectorSize)
{
  // Move to the specified offset
  if (lseek(openedFile, offset, SEEK_SET) == -1)
  {
    perror("Error setting sector offset");
    return -1;
  }

  // Read the sector into the buffer
  ssize_t bytesRead = read(openedFile, buffer, sectorSize);

  if (bytesRead == -1)
  {
    perror("Error reading sector");
    return -1;
  }

  return bytesRead;
}

// Function to decode the time values in a directory entry
void extractTime(uint16_t packedTime, int *hour, int *minute, int *second)
{
  *hour = (packedTime >> 11) & 0x1F;
  *minute = (packedTime >> 5) & 0x3F;
  *second = (packedTime & 0x1F) * 2;
}

// Function to decode a Unicode character
char decodeUnicode(const uint8_t *unicodeChar)
{
  // Assuming the Unicode character is in ASCII range
  if (unicodeChar[1] == 0 && isprint(unicodeChar[0]))
  {
    return unicodeChar[0];
  }
  else
  {
    return '@'; // Return random character for invalid characters
  }
}

// Temporary buffer to add and store long name entries
char longName[256] = {0};
int currentPos = 0;

// Temporary buffer to store each part of a singular long name entry
#define MAX_NAME_PARTS 50
char allNameParts[MAX_NAME_PARTS][14] = {0};
int namePartsCount = 0;

// Global array to hold decoded directory entries and their count
FullDirectoryEntry fullEntry[1000];
size_t numRootDirectoryEntries = 0;

// Function to decode the details of a directory
void decodeDirectoryEntry(DirectoryEntry *entry)
{
  // Create a union to overlay memory
  EntryUnion entryUnion;
  memcpy(&entryUnion.dirEntry, entry, sizeof(DirectoryEntry));

  // Check if the entry is a LFN entry
  if (((entry->DIR_Attr & 0x0F) == 0x0F) || ((entry->DIR_Attr & 0x08) && (entry->DIR_Attr & 0x04)))
  {
    char namePart[14] = {0}; // Temporary buffer to store the name part
    int partPos = 0;         // Position in the name part buffer

    // Decode the name parts from the LFN entry and store them in the temporary buffer
    for (int i = 0; i < 5; i++)
    {
      namePart[partPos++] = decodeUnicode(&entryUnion.longDirEntry.LDIR_Name1[i * 2]);
    }
    for (int i = 0; i < 6; i++)
    {
      namePart[partPos++] = decodeUnicode(&entryUnion.longDirEntry.LDIR_Name2[i * 2]);
    }
    for (int i = 0; i < 2; i++)
    {
      namePart[partPos++] = decodeUnicode(&entryUnion.longDirEntry.LDIR_Name3[i * 2]);
    }

    // Copy all parts of a single long name entry to temporary buffer
    strcpy(allNameParts[namePartsCount], namePart);
    namePartsCount++;
  }
  else
  {
    // Decode attributes field
    char attributes[6];
    attributes[0] = (entry->DIR_Attr & 0x20) ? 'A' : '-';
    attributes[1] = (entry->DIR_Attr & 0x10) ? 'D' : '-';
    attributes[2] = (entry->DIR_Attr & 0x08) ? 'V' : '-';
    attributes[3] = (entry->DIR_Attr & 0x04) ? 'S' : '-';
    attributes[4] = (entry->DIR_Attr & 0x02) ? 'H' : '-';
    attributes[5] = (entry->DIR_Attr & 0x01) ? 'R' : '-';

    // Extracting individual components of the date and time
    int year = ((entry->DIR_WrtDate & 0xFE00) >> 9) + 1980;
    int month = (entry->DIR_WrtDate >> 5) & 0x0F;
    int day = entry->DIR_WrtDate & 0x1F;

    // Decoding time entries
    int hour, minute, second;
    extractTime(entry->DIR_WrtTime, &hour, &minute, &second);

    // Appending the long name components to a combined string
    for (int i = namePartsCount - 1; i >= 0; i--)
    {
      strcat(longName, allNameParts[i]);
    }

    // Replace '@' with null terminator
    for (int i = 0; longName[i] != '\0'; ++i)
    {
      if (longName[i] == '@')
      {
        longName[i] = '\0';
        break; // Exit the loop when '@' is encountered
      }
    }

    // Removing white spaces in the short dir entry
    for (size_t i = 0; i < sizeof(entry->DIR_Name); ++i)
    {
      if (entry->DIR_Name[i] == ' ')
      {
        entry->DIR_Name[i] = '\0';
        break;
      }
    }

    // In case that longName isnt executed
    if (longName[0] == 0)
    {
      // Copy entry->DIR_Name to longName
      strcpy((char *)longName, (const char *)entry->DIR_Name);
    }

    // Copy all the decoded entries to the global array holding all of them
    strcpy(fullEntry[numRootDirectoryEntries].DIR_Name, longName);
    strcpy(fullEntry[numRootDirectoryEntries].shortDIR_Name, (char *)entry->DIR_Name);
    fullEntry[numRootDirectoryEntries].DIR_Attr = entry->DIR_Attr;
    for (int i = 0; i < 6; ++i)
    {
      fullEntry[numRootDirectoryEntries].decodedAttributes[i] = attributes[i];
    }
    fullEntry[numRootDirectoryEntries].DIR_FstClusLO = entry->DIR_FstClusLO;
    fullEntry[numRootDirectoryEntries].DIR_FileSize = entry->DIR_FileSize;
    fullEntry[numRootDirectoryEntries].hour = hour;
    fullEntry[numRootDirectoryEntries].minute = minute;
    fullEntry[numRootDirectoryEntries].second = second;
    fullEntry[numRootDirectoryEntries].day = day;
    fullEntry[numRootDirectoryEntries].month = month;
    fullEntry[numRootDirectoryEntries].year = year;
    numRootDirectoryEntries++;

    // Reseting the temporary buffer
    memset(longName, 0, sizeof(longName));
    currentPos = 0;

    // Reset the nameParts array to zero
    namePartsCount = 0;
    memset(allNameParts, 0, sizeof(allNameParts));
  }
}

// Function to print out directory entries
void printFile(const FullDirectoryEntry *entry)
{
  printf(fileDetailsFormat,
         entry->DIR_FstClusLO, entry->hour, entry->minute, entry->second, entry->day, entry->month, entry->year,
         entry->decodedAttributes[0], entry->decodedAttributes[1], entry->decodedAttributes[2],
         entry->decodedAttributes[3], entry->decodedAttributes[4], entry->decodedAttributes[5],
         entry->DIR_FileSize,
         entry->DIR_Name);
}

// Function to loop through a directory and extract its contents
void processDirectoryEntries(int openedFile, off_t rootDirStart, size_t numEntries)
{
  // Read each directory entry
  for (size_t i = 0; i < numEntries; ++i)
  {
    DirectoryEntry entry;
    readSector(openedFile, &entry, rootDirStart + i * sizeof(DirectoryEntry), sizeof(DirectoryEntry));

    // Check if the entry is a valid file or directory
    if ((entry.DIR_Name[0] == 0x00))
    {
      return;
    }
    else if (entry.DIR_Name[0] == 0xE5)
    {
      continue;
    }

    // Pass the entry to be decoded
    decodeDirectoryEntry(&entry);
  }
}

// Function to calculate byte offset for a given cluster
off_t calculateByteOffset(uint16_t clusterNumber, uint16_t bytesPerCluster, off_t dataAreaStart)
{
  // Subtract 2 because cluster numbers start from 2 in FAT systems
  return (clusterNumber - 2) * bytesPerCluster + dataAreaStart;
}

// Function to create a volume structure
Volume *createVolume(int openedFile, BootSector *bootEntries, uint16_t *fatEntries, size_t fatSize, off_t dataAreaStart)
{
  Volume *volume = malloc(sizeof(Volume));
  if (volume == NULL)
  {
    perror("Error allocating memory for Volume structure");
    close(openedFile);
    exit(EXIT_FAILURE);
  }

  volume->openedFile = openedFile;
  volume->bootEntries = bootEntries;
  volume->fatEntries = fatEntries;
  volume->fatSize = fatSize;
  volume->dataAreaStart = dataAreaStart;

  return volume;
}

// Open a file and return a File structure
File *openFile(Volume *volume, FullDirectoryEntry *entry)
{
  File *file = malloc(sizeof(File));

  file->openedFile = volume->openedFile;
  file->bootEntries = volume->bootEntries;
  file->fatEntries = volume->fatEntries;
  file->fatSize = volume->fatSize;
  file->dataAreaStart = volume->dataAreaStart;
  file->currentClusterOffset = 0;
  file->currentCluster = entry->DIR_FstClusLO;
  file->clusterArray = NULL; // Initialize to NULL initially
  file->clusterArraySize = 0;
  file->fileSize = entry->DIR_FileSize;

  // Populate the cluster array
  size_t currentCluster = entry->DIR_FstClusLO;
  while (currentCluster < 0xfff8)
  {
    file->clusterArray = realloc(file->clusterArray, (file->clusterArraySize + 1) * sizeof(size_t));

    file->clusterArray[file->clusterArraySize] = currentCluster;
    file->clusterArraySize++;

    currentCluster = file->fatEntries[currentCluster];
  }

  return file;
}

// Seek to a specified offset within the file
off_t seekFile(File *file, off_t offset, int whence)
{
  BootSector bootSector = *(file->bootEntries);

  // Update current cluster offset based on the offset and whence
  if (whence == SEEK_SET)
  {
    file->currentClusterOffset = offset;
  }
  else if (whence == SEEK_CUR)
  {
    file->currentClusterOffset += offset;
  }
  else if (whence == SEEK_END)
  {
    // Calculate the file size
    size_t fileSize = file->fileSize;

    // Update the current cluster offset based on the file size and offset
    file->currentClusterOffset = fileSize - offset;
  }

  // Update the current cluster based on the offset
  size_t clusterIndex = file->currentClusterOffset / (bootSector.BPB_BytsPerSec * bootSector.BPB_SecPerClus);

  // Check if the clusterIndex is within the bounds of the clusterArray
  if (clusterIndex < file->clusterArraySize)
  {
    file->currentCluster = file->clusterArray[clusterIndex];
  }
  else
  {
    // The seek operation is beyond the end of the file
    file->currentCluster = 0xfff8; // Set it to an invalid value
  }

  return file->currentClusterOffset;
}

// Read data from the file into the buffer
size_t readFile(File *file, void *buffer, size_t length)
{
  BootSector bootEntries = *(file->bootEntries);
  size_t bytesRead = 0; // Total bytes read

  while (file->currentCluster < 0xfff8 && bytesRead < length)
  {
    // Calculate the offset within the current cluster
    off_t clusterOffset = file->currentClusterOffset % (bootEntries.BPB_BytsPerSec * bootEntries.BPB_SecPerClus);

    // Calculate the offset within the data area
    off_t dataOffset = file->dataAreaStart + ((file->currentCluster - 2) * bootEntries.BPB_BytsPerSec * bootEntries.BPB_SecPerClus) + clusterOffset;

    // Calculate the number of bytes available in the current cluster
    size_t bytesAvailable = (bootEntries.BPB_BytsPerSec * bootEntries.BPB_SecPerClus) - clusterOffset;

    // Calculate the number of bytes to read in this iteration
    size_t bytesToRead = (length - bytesRead) < bytesAvailable ? (length - bytesRead) : bytesAvailable;

    // Seek to the calculated data offset
    lseek(file->openedFile, dataOffset, SEEK_SET);

    // Read data from the file into the buffer
    size_t bytesReadFromCluster = read(file->openedFile, (char *)buffer + bytesRead, bytesToRead);

    // Update the current cluster offset and total bytes read
    file->currentClusterOffset += bytesReadFromCluster;
    bytesRead += bytesReadFromCluster;

    // Move to the next cluster in the FAT chain
    file->currentCluster = file->fatEntries[file->currentCluster];
  }

  return bytesRead;
}

// Close the file and free resources
void closeFile(File *file)
{
  // Free the data
  free(file->clusterArray);
  free(file);
}

// Temporary integer to hold current directory index
int newDirectoryIndex = 0;

// Global array to hold existing cluster locations to prevent overlapping
size_t clusterCheckArray[100];
int maxClusterCheckNumber = 100;

// Global array to hold existing directory locations to prevent overlapping
int directoryNumbers[100];

// Function to find a file/directory in the file system
FullDirectoryEntry *findDirectoryEntryInDirectory(int openedFile, BootSector *bootSector, uint16_t *fatEntries, off_t dataAreaStart, FullDirectoryEntry *parentDirectory, int *currentDirectoryIndex, const char *name)
{
  // Iterate through the directory entries in the parent directory
  for (size_t i = *currentDirectoryIndex; i < numRootDirectoryEntries; ++i)
  {
    FullDirectoryEntry *entry = &parentDirectory[i];

    // Check if the current entry matches the desired name based on either long name or short name
    if ((strcmp(entry->DIR_Name, name) == 0) || strncmp(entry->shortDIR_Name, name, strlen(name)) == 0)
    {
      // If the entry is a subdirectory, recursively search inside it and populate global array
      if ((entry->DIR_Attr & 0x10) && !(entry->DIR_Attr & 0x08))
      {
        size_t clusterValue = entry->DIR_FstClusLO;
        newDirectoryIndex = numRootDirectoryEntries;

        // Loop to prevent overlap from exisiting entries
        if (clusterValue == 0)
        {
          newDirectoryIndex = 0;
        }
        else
        {
          // Check if clusterValue already exists in clusterCheckArray
          int existingIndex = -1;
          for (int i = 0; i < maxClusterCheckNumber; i++)
          {
            if (clusterCheckArray[i] == clusterValue)
            {
              existingIndex = i;
              break;
            }
          }

          if (existingIndex != -1)
          {
            // If clusterValue already exists, update newDirectoryIndex
            newDirectoryIndex = directoryNumbers[existingIndex];
          }
          else
          {
            // If not, add the current clusterValue and newDirectoryIndex to the arrays
            for (int i = 0; i < maxClusterCheckNumber; i++)
            {
              if (clusterCheckArray[i] == 0)
              {
                directoryNumbers[i] = newDirectoryIndex;
                clusterCheckArray[i] = clusterValue;

                // Go through the cluster chain and populate array with its entries
                while (clusterValue < 0xfff8)
                {
                  uint16_t bytesPerCluster = bootSector->BPB_SecPerClus * bootSector->BPB_BytsPerSec;
                  off_t testOffset = calculateByteOffset(clusterValue, bytesPerCluster, dataAreaStart);
                  size_t numEntries = bytesPerCluster / sizeof(DirectoryEntry);

                  processDirectoryEntries(openedFile, testOffset, numEntries);
                  clusterValue = fatEntries[clusterValue];
                }
                break;
              }
            }
          }
        }
        // Return found entry
        return entry;
      }
      else
      {
        // Return the found entry
        return entry;
      }
    }
  }

  // If the entry is not found, return NULL
  return NULL;
}

// Function to find a directory entry based on the given path recursively
FullDirectoryEntry *findDirectoryEntryByPath(int openedFile, BootSector *bootSector, uint16_t *fatEntries, off_t dataAreaStart, const char *path)
{
  // Split the path into individual components
  char *token, *pathCopy;
  pathCopy = strdup(path);
  token = strtok_r(pathCopy, "/", &pathCopy);

  // Temporary entry to hold the returned value
  FullDirectoryEntry *currentEntry = fullEntry;
  newDirectoryIndex = 0;

  while (1)
  {
    // Recursive call to find the next entry
    currentEntry = findDirectoryEntryInDirectory(openedFile, bootSector, fatEntries, dataAreaStart, fullEntry, &newDirectoryIndex, token);

    if (NULL == currentEntry)
    {
      // does not exist
      return currentEntry;
    }
    else
    {
      // Check if returned entry is a directory or not
      if ((currentEntry->DIR_Attr & 0x10) && !(currentEntry->DIR_Attr & 0x08))
      {
        // Get the next token
        token = strtok_r(NULL, "/", &pathCopy);
        if (NULL == token)
        {
          // If end of string
          return currentEntry;
        }
      }
      else
      {
        return currentEntry;
      }
    }
  }
}

// Function to print the decoded values of a found entry and its content
void processEntry(Volume *volume, FullDirectoryEntry *newEntry, FullDirectoryEntry *fullEntry, int numRootDirectoryEntries, int newDirectoryIndex, const char *newFilePath)
{
  if (newEntry != NULL)
  {
    printf("\nEntry found for path: %s\n\n", newFilePath);
    printf(headerFormat, "First Cluster", "Last Modified Time", "Last Modified Date", "Attributes", "Length", "FileName");
    printf("--------------------------------------------------");
    printf("----------------------------------------------------\n");
    printFile(newEntry);

    printf("\n==================================================");
    printf("====================================================");
    printf("\nCONTENTS OF %s\n", newEntry->DIR_Name);

    // If its a directory prints its sub content (files or directories)
    if ((newEntry->DIR_Attr & 0x10) && !(newEntry->DIR_Attr & 0x08))
    {
      for (int x = 0; x < numRootDirectoryEntries; x++)
      {
        if (strcmp(fullEntry[x].DIR_Name, newEntry->DIR_Name) == 0)
        {
          for (int y = newDirectoryIndex; y < numRootDirectoryEntries; y++)
          {
            printFile(&fullEntry[y]);
          }
        }
      }
    }
    else // Else print the files output
    {
      File *file = openFile(volume, newEntry);

      // Seek to the beginning of the file
      seekFile(file, 0, SEEK_SET);

      // Ask the user for the buffer size
      size_t bufferSize;
      printf("Enter the buffer size: ");
      scanf("%zu", &bufferSize);

      // Read and print the content of the file
      char *buffer = (char *)malloc(bufferSize);
      if (buffer == NULL)
      {
        perror("Error allocating memory for buffer");
        exit(EXIT_FAILURE);
      }

      size_t bytesRead = readFile(file, buffer, bufferSize);

      // Print the data read from the file
      for (size_t i = 0; i < bytesRead; ++i)
      {
        printf("%c", buffer[i]);
      }
      printf("\n");

      // Free allocated memory
      free(buffer);
    }
  }
  else
  {
    printf("Entry not found for path: %s\n", newFilePath);
  }
}

int main(int argc, char *argv[])
{
  // Check if command line arguements are correct
  if (argc != 2)
  {
    fprintf(stderr, "Usage: %s <path to fat16 image>\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  // FAT16 filepath
  const char *filePath = argv[1];

  // Open the fat16 file
  int openedFile = open(filePath, O_RDONLY);
  if (openedFile == -1)
  {
    perror("Error opening file");
    exit(EXIT_FAILURE);
  }

  // Task 2

  // Read the Boot Sector to get necessary information
  BootSector bootSector;
  readSector(openedFile, &bootSector, 0, sizeof(BootSector));

  // Task 3

  // Calculate the start of the FAT based on the number of reserved sectors
  off_t fatStart = bootSector.BPB_RsvdSecCnt * bootSector.BPB_BytsPerSec;

  // Calculate the size of one FAT in bytes
  size_t fatSize = bootSector.BPB_FATSz16 * bootSector.BPB_BytsPerSec;

  // Allocate memory to store FAT entries
  uint16_t *fatEntries = malloc(fatSize);
  readSector(openedFile, fatEntries, fatStart, fatSize);

  // Task 4

  // Calculate the start of the root directory
  off_t rootDirStart = (bootSector.BPB_RsvdSecCnt + bootSector.BPB_NumFATs * bootSector.BPB_FATSz16) * bootSector.BPB_BytsPerSec;

  // Set the file pointer to the start of the root directory
  lseek(openedFile, rootDirStart, SEEK_SET);

  // Calculate the number of entries in the root directory
  uint16_t bytesPerCluster = bootSector.BPB_SecPerClus * bootSector.BPB_BytsPerSec; // Calculate bytes per cluster
  size_t numEntries = bytesPerCluster / sizeof(DirectoryEntry);

  // Process directory entries using the separate function
  processDirectoryEntries(openedFile, rootDirStart, numEntries);

  off_t dataAreaStart = (bootSector.BPB_RsvdSecCnt + bootSector.BPB_NumFATs * bootSector.BPB_FATSz16 + (bootSector.BPB_RootEntCnt * sizeof(DirectoryEntry) + bootSector.BPB_BytsPerSec - 1) / bootSector.BPB_BytsPerSec) * bootSector.BPB_BytsPerSec;

  // const char *newFilePath = "/man/man2/../../man/./././../man/man2/../../sessions.txt";

  // Taking in user input for file path
  char newFilePath[256]; // Adjust the size based on your requirements
  printf("Enter the file path: ");
  fgets(newFilePath, sizeof(newFilePath), stdin);

  // Remove the trailing newline character if present
  size_t len = strlen(newFilePath);
  if (len > 0 && newFilePath[len - 1] == '\n')
  {
    newFilePath[len - 1] = '\0';
  }

  Volume *volume = createVolume(openedFile, &bootSector, fatEntries, fatSize, dataAreaStart);
  FullDirectoryEntry *newEntry = findDirectoryEntryByPath(openedFile, &bootSector, fatEntries, dataAreaStart, newFilePath);
  processEntry(volume, newEntry, fullEntry, numRootDirectoryEntries, newDirectoryIndex, newFilePath);

  // Free allocated memory
  free(fatEntries);

  // Close the file
  close(openedFile);

  return 0;
}
