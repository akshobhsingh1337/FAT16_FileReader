# FAT16 Reader

A C program for reading and exploring FAT16 file systems.

## Description

This FAT16 Reader is a command-line tool that allows users to explore and read the contents of FAT16 file systems. It provides functionality to:

- Read and parse the boot sector of a FAT16 image
- Navigate through directories
- List file and directory contents
- Read file contents

The program is designed to work with FAT16 disk images and provides a detailed view of the file system structure.

## Features

- Parse and display FAT16 boot sector information
- Navigate through directory structures
- Display file and directory attributes
- Read and display file contents
- Support for long file names (LFN)

## Prerequisites

- GCC compiler
- POSIX-compliant operating system (Linux, macOS, etc.)

## Compilation and Running

To compile the program, use the following command:

```sh
gcc -o fat16_reader Fat16_Reader.c
```

To run the program use:

```sh
./fat16_reader <path_to_fat16_image>
```

## Usage

Once the program is running, you can use the following commands:

- `ls`: List the contents of the current directory
- `cd <directory>`: Change to the specified directory
- `cat <file>`: Display the contents of the specified file
- `info`: Display information about the FAT16 file system
- `exit`: Exit the program

## Limitations

- This program is designed for educational purposes and may not handle all edge cases in real-world FAT16 file systems.
- It does not support writing or modifying the file system.
- Large files may take longer to read and display.

## Contributing

Contributions to improve the FAT16 Reader are welcome. Please feel free to submit pull requests or open issues for bugs and feature requests.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.