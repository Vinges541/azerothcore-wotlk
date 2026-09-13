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

#ifndef MPQ_H
#define MPQ_H

#include "libmpq/mpq.h"
#include "loadlib/loadlib.h"
#include <cstring>
#include <deque>
#include <iostream>
#include <string>
#include <vector>

using namespace std;

class MPQArchive
{
public:
    mpq_archive_s* mpq_a = nullptr;

    MPQArchive(char const* filename);
    ~MPQArchive() { if (isOpened()) close(); }
    bool isLoose() const { return !_looseRoot.empty(); }
    std::string const& getLooseRoot() const { return _looseRoot; }

    void GetFileListTo(vector<string>& filelist);

private:
    bool _closed = true;
    std::string _looseRoot;

    void close();
    bool isOpened() const;
};
typedef std::deque<MPQArchive*> ArchiveSet;

class MPQFile
{
    //MPQHANDLE handle;
    bool eof;
    char* buffer;
    libmpq__off_t pointer, size;

    // disable copying
    MPQFile(MPQFile const& /*f*/) {}
    void operator=(MPQFile const& /*f*/) {}

public:
    MPQFile(char const* filename);    // filenames are not case sensitive
    ~MPQFile() { close(); }
    std::size_t read(void* dest, std::size_t bytes);
    std::size_t getSize() { return size; }
    std::size_t getPos() { return pointer; }
    char* getBuffer() { return buffer; }
    char* getPointer() { return buffer + pointer; }
    bool isEof() { return eof; }
    void seek(int offset);
    void seekRelative(int offset);
    void close();
};

inline void flipcc(char* fcc)
{
    std::swap(fcc[0], fcc[3]);
    std::swap(fcc[1], fcc[2]);
}

#endif
