// Copyright Contributors to the OpenVDB Project
// SPDX-License-Identifier: MPL-2.0

/*!
    \file   nanovdb_convert.cpp

    \author Ken Museth

    \date   May 21, 2020

    \brief  Command-line tool that converts between openvdb and nanovdb files
*/

#include <string>
#include <algorithm>
#include <cctype>

#include <nanovdb/util/IO.h> // this is required to read (and write) NanoVDB files on the host
#include <nanovdb/util/OpenToNanoVDB.h>
#include <nanovdb/util/NanoToOpenVDB.h>

void usage [[noreturn]] (const std::string& progName, int exitStatus = EXIT_FAILURE)
{
    std::cerr << "\nUsage: " << progName << " [options] *.vdb output.nvdb\n"
              << "Which: converts one or more OpenVDB files to a single NanoVDB file\n\n"
              << "Usage: " << progName << " [options] *.nvdb output.vdb\n"
              << "Which: converts one or more NanoVDB files to a single OpenVDB file\n\n"
              << "Options:\n"
              << "-b,--blosc\tUse BLOSC compression on the output file\n"
              << "-c,--checksum mode\t where mode={none, partial, full}\n"
              << "-f,--force\tOverwrite output file if it already exists\n"
              << "-g,--grid name\tConvert all grids matching the specified string name\n"
              << "-h,--help\tPrints this message\n"
              << "-s,--stats mode\t where mode={none, bbox, extrema, all}\n"
              << "-v,--verbose\tPrint verbose information to the terminal\n"
              << "-z,--zip\tUse ZIP compression on the output file\n";
    exit(exitStatus);
}

int main(int argc, char* argv[])
{
    int exitStatus = EXIT_SUCCESS;

    nanovdb::io::Codec       codec = nanovdb::io::Codec::NONE;
    nanovdb::StatsMode       sMode = nanovdb::StatsMode::Default;
    nanovdb::ChecksumMode    cMode = nanovdb::ChecksumMode::Default;
    bool                     verbose = false, overwrite = false;
    std::string              gridName;
    std::vector<std::string> fileNames;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg[0] == '-') {
            if (arg == "-v" || arg == "--verbose") {
                verbose = true;
            } else if (arg == "-f" || arg == "--force") {
                overwrite = true;
            } else if (arg == "-h" || arg == "--help") {
                usage(argv[0], EXIT_SUCCESS);
            } else if (arg == "-b" || arg == "--blosc") {
                codec = nanovdb::io::Codec::BLOSC;
            } else if (arg == "-z" || arg == "--zip") {
                codec = nanovdb::io::Codec::ZIP;
            } else if (arg == "-c" || arg == "--checksum") {
                if (i + 1 == argc) {
                    std::cerr << "Expected a mode to follow the -c,--checksum option\n" << std::endl;
                    usage(argv[0]);
                } else {
                    std::string str(argv[++i]);
                    std::transform(str.begin(), str.end(), str.begin(),[](unsigned char c){ return std::tolower(c); });
                    if (str == "none") {
                       cMode = nanovdb::ChecksumMode::Disable;
                    } else if (str == "partial") {
                       cMode = nanovdb::ChecksumMode::Partial;
                    } else if (str == "full") {
                       cMode = nanovdb::ChecksumMode::Full;
                    } else {
                      std::cerr << "Expected one of the following checksum modes: {none, partial, full}\n" << std::endl;
                      usage(argv[0]);
                    }
                }
            } else if (arg == "-s" || arg == "--stats") {
                if (i + 1 == argc) {
                    std::cerr << "Expected a mode to follow the -s,--stats option\n" << std::endl;
                    usage(argv[0]);
                } else {
                    std::string str(argv[++i]);
                    std::transform(str.begin(), str.end(), str.begin(),[](unsigned char c){ return std::tolower(c); });
                    if (str == "none") {
                       sMode = nanovdb::StatsMode::Disable;
                    } else if (str == "bbox") {
                       sMode = nanovdb::StatsMode::BBox;
                    } else if (str == "extrema") {
                       sMode = nanovdb::StatsMode::MinMax;
                    } else if (str == "all") {
                       sMode = nanovdb::StatsMode::All;
                    } else {
                      std::cerr << "Expected one of the following stats modes: {none, bbox, extrema, all}\n" << std::endl;
                      usage(argv[0]);
                    }
                }    
            } else if (arg == "-g" || arg == "--grid") {
                if (i + 1 == argc) {
                    std::cerr << "Expected a grid name to follow the -g,--grid option\n" << std::endl;
                    usage(argv[0]);
                } else {
                    gridName.assign(argv[++i]);
                }
            } else {
                std::cerr << "Unrecognized option: \"" << arg << "\"\n" << std::endl;
                usage(argv[0]);
            }
        } else if (!arg.empty()) {
            fileNames.push_back(arg);
        }
    }
    if (fileNames.size() < 2) {
        std::cerr << "Expected at least an input file followed by exactly one output file\n" << std::endl;
        usage(argv[0]);
    }
    const std::string outputFile = fileNames.back();
    const std::string ext = outputFile.substr(outputFile.find_last_of(".") + 1);
    bool              toNanoVDB = false;
    if (ext == "nvdb") {
        toNanoVDB = true;
    } else if (ext != "vdb") {
        std::cerr << "Unrecognized file extension: \"" << ext << "\"\n" << std::endl;
        usage(argv[0]);
    }

    fileNames.pop_back();

    if (!overwrite) {
        std::ifstream is(outputFile, std::ios::in | std::ios::binary);
        if (is.peek() != std::ifstream::traits_type::eof()) {
            std::cout << "Overwrite the existing output file named \"" << outputFile << "\"? [Y]/N: ";
            std::string answer;
            getline(std::cin, answer);
            if (!answer.empty() && answer != "Y" && answer != "y" && answer != "yes" && answer != "YES") {
                std::cout << "Please specify a different output file" << std::endl;
                exit(EXIT_SUCCESS);
            }
        }
    }

    openvdb::initialize();

    // Note, unlike OpenVDB, NanoVDB allows for multiple write operations into the same output file stream.
    // Hence, NanoVDB grids can be read, converted and written to file one at a time whereas all
    // the OpenVDB grids has to be written to file in a single operation.

    try {
        if (toNanoVDB) { // OpenVDB -> NanoVDB
            std::ofstream os(outputFile, std::ios::out | std::ios::binary);
            for (auto& inputFile : fileNames) {
                if (inputFile.substr(inputFile.find_last_of(".") + 1) != "vdb") {
                    std::cerr << "Since the last file has extension .nvdb the remaining input files were expected to have extensions .vdb\n" << std::endl;
                    usage(argv[0]);
                }
                if (verbose)
                    std::cout << "Opening OpenVDB file named \"" << inputFile << "\"" << std::endl;
                openvdb::io::File file(inputFile);
                file.open(false); //disable delayed loading
                if (gridName.empty()) {
                    auto grids = file.getGrids();
                    for (auto& grid : *grids) {
                        if (verbose) {
                            std::cout << "Converting OpenVDB grid named \"" << grid->getName() << "\" to NanoVDB" << std::endl;
                        }
                        auto handle = nanovdb::openToNanoVDB(grid, sMode, cMode, false, verbose ? 1 : 0);
                        nanovdb::io::writeGrid(os, handle, codec);
                    } // loop over OpenVDB grids in file
                } else {
                    auto grid = file.readGrid(gridName);
                    if (verbose) {
                        std::cout << "Converting OpenVDB grid named \"" << grid->getName() << "\" to NanoVDB" << std::endl;
                    }
                    auto handle = nanovdb::openToNanoVDB(grid, sMode, cMode, false, verbose ? 1 : 0);
                    nanovdb::io::writeGrid(os, handle, codec);
                }
            } // loop over input files
        } else { // NanoVDB -> OpenVDB
            openvdb::io::File      file(outputFile);
            openvdb::GridPtrVecPtr grids(new openvdb::GridPtrVec());
            for (auto& inputFile : fileNames) {
                if (inputFile.substr(inputFile.find_last_of(".") + 1) != "nvdb") {
                    std::cerr << "Since the last file has extension .vdb the remaining input files were expected to have extensions .nvdb\n" << std::endl;
                    usage(argv[0]);
                }
                if (verbose)
                    std::cout << "Opening NanoVDB file named \"" << inputFile << "\"" << std::endl;
                if (gridName.empty()) {
                    auto handles = nanovdb::io::readGrids(inputFile, verbose);
                    for (auto &h : handles) {
                        if (verbose)
                            std::cout << "Converting NanoVDB grid named \"" << h.gridMetaData()->gridName() << "\" to OpenVDB" << std::endl;
                        grids->push_back(nanoToOpenVDB(h));
                    }
                } else {
                    auto handle = nanovdb::io::readGrid(inputFile, gridName);
                    if (!handle) {
                        std::cerr << "File did not contain a NanoVDB grid named \"" << gridName << "\"\n" << std::endl;
                        usage(argv[0]);
                    }
                    if (verbose)
                        std::cout << "Converting NanoVDB grid named \"" << handle.gridMetaData()->gridName() << "\" to OpenVDB" << std::endl;
                    grids->push_back(nanoToOpenVDB(handle));
                }
            } // loop over input files
            file.write(*grids);
        }
        if (verbose) {
            std::cout << "\nThis binary was build against NanoVDB version " << NANOVDB_MAJOR_VERSION_NUMBER << "."
                                                                            << NANOVDB_MINOR_VERSION_NUMBER << "."
                                                                            << NANOVDB_PATCH_VERSION_NUMBER << std::endl;
        }
    }
    catch (const std::exception& e) {
        std::cerr << "An exception occurred: \"" << e.what() << "\"" << std::endl;
        exitStatus = EXIT_FAILURE;
    }
    catch (...) {
        std::cerr << "Exception oof unexpected type caught" << std::endl;
        exitStatus = EXIT_FAILURE;
    }

    return exitStatus;
}