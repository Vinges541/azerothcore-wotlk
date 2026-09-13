/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "mpq_libmpq04.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <system_error>

ArchiveSet gOpenArchives;

namespace
{
std::string Lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
    {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::filesystem::path ResolveLooseFile(std::filesystem::path const& root, char const* filename)
{
    std::string normalized(filename);
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    std::filesystem::path relative = std::filesystem::path(normalized).lexically_normal();
    if (relative.is_absolute())
        return {};

    for (std::filesystem::path const& part : relative)
        if (part == "..")
            return {};

    std::error_code error;
    std::filesystem::path direct = root / relative;
    if (std::filesystem::is_regular_file(direct, error))
        return direct;

    std::filesystem::path current = root;
    for (std::filesystem::path const& part : relative)
    {
        if (part == ".")
            continue;

        std::filesystem::path match;
        std::string wanted = Lowercase(part.string());
        for (std::filesystem::directory_iterator iterator(current, error), end; !error && iterator != end; ++iterator)
        {
            if (Lowercase(iterator->path().filename().string()) == wanted)
            {
                match = iterator->path();
                break;
            }
        }
        if (match.empty())
            return {};
        current = match;
    }

    return std::filesystem::is_regular_file(current, error) ? current : std::filesystem::path{};
}

bool ReadLooseFile(std::filesystem::path const& root, char const* filename, char*& buffer, libmpq__off_t& size)
{
    std::filesystem::path path = ResolveLooseFile(root, filename);
    if (path.empty())
        return false;

    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
        return false;

    std::streampos endPosition = input.tellg();
    if (endPosition < 0)
        return false;
    std::streamsize fileSize = static_cast<std::streamsize>(endPosition);
    if (fileSize <= 1)
    {
        size = static_cast<libmpq__off_t>(fileSize);
        return true;
    }

    input.seekg(0);
    buffer = new char[static_cast<std::size_t>(fileSize)];
    if (!input.read(buffer, fileSize))
    {
        delete[] buffer;
        buffer = nullptr;
        return false;
    }

    size = static_cast<libmpq__off_t>(fileSize);
    return true;
}
}

MPQArchive::MPQArchive(char const* filename)
{
    printf("Opening %s\n", filename);
    std::error_code error;
    if (std::filesystem::is_directory(filename, error))
    {
        _looseRoot = std::filesystem::absolute(filename, error).string();
        _closed = false;
        gOpenArchives.push_front(this);
        printf("Mounted loose patch directory %s\n", filename);
        return;
    }

    int result = libmpq__archive_open(&mpq_a, filename, -1);
    if (result)
    {
        switch (result)
        {
            case LIBMPQ_ERROR_OPEN :
                printf("Error opening archive '%s': Does file really exist?\n", filename);
                break;
            case LIBMPQ_ERROR_FORMAT :            /* bad file format */
                printf("Error opening archive '%s': Bad file format\n", filename);
                break;
            case LIBMPQ_ERROR_SEEK :         /* seeking in file failed */
                printf("Error opening archive '%s': Seeking in file failed\n", filename);
                break;
            case LIBMPQ_ERROR_READ :              /* Read error in archive */
                printf("Error opening archive '%s': Read error in archive\n", filename);
                break;
            case LIBMPQ_ERROR_MALLOC :               /* maybe not enough memory? :) */
                printf("Error opening archive '%s': Maybe not enough memory\n", filename);
                break;
            default:
                printf("Error opening archive '%s': Unknown error\n", filename);
                break;
        }
        return;
    }
    _closed = false;
    gOpenArchives.push_front(this);
}

bool MPQArchive::isOpened() const
{
    return !_closed && std::find(gOpenArchives.begin(), gOpenArchives.end(), this) != gOpenArchives.end();
}

void MPQArchive::close()
{
    if (_closed)
        return;
    if (mpq_a)
        libmpq__archive_close(mpq_a);
    mpq_a = nullptr;
    _closed = true;
}

void MPQArchive::GetFileListTo(vector<string>& filelist)
{
    if (isLoose())
    {
        std::error_code error;
        std::filesystem::path root(_looseRoot);
        for (std::filesystem::recursive_directory_iterator iterator(root, error), end;
            !error && iterator != end; ++iterator)
        {
            if (!iterator->is_regular_file(error))
                continue;
            std::string relative = std::filesystem::relative(iterator->path(), root, error).generic_string();
            std::replace(relative.begin(), relative.end(), '/', '\\');
            filelist.push_back(relative);
        }
        return;
    }

    uint32_t filenum;
    if (libmpq__file_number(mpq_a, "(listfile)", &filenum))
        return;
    libmpq__off_t size, transferred;
    libmpq__file_unpacked_size(mpq_a, filenum, &size);

    char* buffer = new char[size + 1];
    buffer[size] = '\0';
    libmpq__file_read(mpq_a, filenum, reinterpret_cast<unsigned char*>(buffer), size, &transferred);

    char seps[] = "\n";
    char* token = strtok(buffer, seps);
    uint32 counter = 0;
    while (token != nullptr && counter < size)
    {
        token[strlen(token) - 1] = 0;
        filelist.emplace_back(token);
        counter += strlen(token) + 2;
        token = strtok(nullptr, seps);
    }

    delete[] buffer;
}

MPQFile::MPQFile(char const* filename):
    eof(false),
    buffer(nullptr),
    pointer(0),
    size(0)
{
    std::string archiveFilename(filename);
    std::replace(archiveFilename.begin(), archiveFilename.end(), '/', '\\');
    for (auto & gOpenArchive : gOpenArchives)
    {
        if (gOpenArchive->isLoose())
        {
            if (!ReadLooseFile(gOpenArchive->getLooseRoot(), filename, buffer, size))
                continue;
            if (size <= 1)
            {
                eof = true;
                return;
            }
            return;
        }

        mpq_archive* mpq_a = gOpenArchive->mpq_a;

        uint32 filenum;
        if (libmpq__file_number(mpq_a, archiveFilename.c_str(), &filenum)) continue;
        libmpq__off_t transferred;
        libmpq__file_unpacked_size(mpq_a, filenum, &size);

        // HACK: in patch.mpq some files don't want to open and give 1 for filesize
        if (size <= 1)
        {
            // printf("info: file %s has size %d; considered dummy file.\n", filename, size);
            eof = true;
            buffer = nullptr;
            return;
        }
        buffer = new char[size];

        //libmpq_file_getdata
        libmpq__file_read(mpq_a, filenum, reinterpret_cast<unsigned char*>(buffer), size, &transferred);
        /*libmpq_file_getdata(&mpq_a, hash, fileno, (unsigned char*)buffer);*/
        return;
    }
    eof = true;
    buffer = nullptr;
}

std::size_t MPQFile::read(void* dest, std::size_t bytes)
{
    if (eof) return 0;

    std::size_t rpos = pointer + bytes;
    if (rpos > std::size_t(size))
    {
        bytes = size - pointer;
        eof = true;
    }

    memcpy(dest, &(buffer[pointer]), bytes);

    pointer = rpos;

    return bytes;
}

void MPQFile::seek(int offset)
{
    pointer = offset;
    eof = (pointer >= size);
}

void MPQFile::seekRelative(int offset)
{
    pointer += offset;
    eof = (pointer >= size);
}

void MPQFile::close()
{
    delete[] buffer;
    buffer = nullptr;
    eof = true;
}
