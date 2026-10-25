/*
    ------------------------------------------------------------------------------------
    LICENSE:
    ------------------------------------------------------------------------------------
    This file is part of EVEmu: EVE Online Server Emulator
    Copyright 2006 - 2021 The EVEmu Team
    For the latest information visit https://evemu.dev
    ------------------------------------------------------------------------------------
    This program is free software; you can redistribute it and/or modify it under
    the terms of the GNU Lesser General Public License as published by the Free Software
    Foundation; either version 2 of the License, or (at your option) any later
    version.

    This program is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License along with
    this program; if not, write to the Free Software Foundation, Inc., 59 Temple
    Place - Suite 330, Boston, MA 02111-1307, USA, or go to
    http://www.gnu.org/copyleft/lesser.txt.
    ------------------------------------------------------------------------------------
    Author:        caytchen
*/

/** @todo  boost is the only system in this code that does NOT leak */

#include "imageserver/ImageServer.h"
#include "imageserver/ImageServerListener.h"

#include <algorithm>
#include <filesystem>

const char *const ImageServer::FallbackURL = "http://image.eveonline.com/";

const char *const ImageServer::Categories[] = {
    "Alliance",
    "Corporation",
    "Character",
    "InventoryType",
    "Render" };

const uint32 ImageServer::CategoryCount = 5;

// true for hosts that only ever mean "this machine" to whoever resolves them
static bool IsLoopbackHost(const std::string& host)
{
    std::string lower(host);
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return (lower == "localhost") or (lower.compare(0, 4, "127.") == 0) or (lower == "::1") or (lower == "[::1]");
}

// true when this process runs inside a docker/podman container, where the address a connection arrives on is the
//  container's own (not reachable by players) and a published loopback port may be the only working image server url
static bool RunningInContainer()
{
#ifdef _WIN32
    return false;
#else
    std::error_code ec;
    return std::filesystem::exists("/.dockerenv", ec) or std::filesystem::exists("/run/.containerenv", ec);
#endif
}

ImageServer::ImageServer()
: _autoHost(false)
{
    /* the client downloads every portrait but its own (which it keeps from character creation) from this url,
     *  so it must name this server as the *client* sees it.  "auto" (or empty) uses the address each client connected to.
     *  a loopback host is treated the same unless we are in a container, as it sends every remote client to its own machine.
     */
    std::string host(sConfig.net.imageServer);
    host.erase(0, host.find_first_not_of(" \t\r\n"));
    host.erase(host.find_last_not_of(" \t\r\n") + 1);
    std::string lower(host);
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower.empty() or (lower == "auto")) {
        _autoHost = true;
        host = "127.0.0.1";
    } else if (IsLoopbackHost(host)) {
        if (RunningInContainer()) {
            sLog.Warning("      ImageServer", "<imageServer> is %s: only a client on the docker host itself can load other characters' portraits.", host.c_str());
            sLog.Warning("      ImageServer", "  set <imageServer> in eve-server.xml to the address players connect to (the host's LAN/public ip or name).");
        } else {
            _autoHost = true;
        }
    }

    std::stringstream urlBuilder;
    urlBuilder << "http://" << host << ":" << sConfig.net.imageServerPort << "/";
    _url = urlBuilder.str();

    _basePath = sConfig.files.imageDir;
    if (_basePath[_basePath.size() - 1] != '/')
        _basePath += "/";

    if (_autoHost) {
        sLog.Cyan("      ImageServer", "Image Server URL: http://<address the client connected to>:%u/ (<imageServer> %s)", (uint32)sConfig.net.imageServerPort,sConfig.net.imageServer.c_str());
    } else {
        sLog.Cyan("      ImageServer", "Image Server URL: %s", _url.c_str());
    }
    sLog.Cyan("      ImageServer", "Image Server path: %s", _basePath.c_str());

    // create the base directory and every category directory (existing ones are fine)
    for (int i = 0; i < CategoryCount; i++) {
        std::string subdir = _basePath;
        subdir.append(Categories[i]);
        std::error_code ec;
        std::filesystem::create_directories(subdir, ec);
        if (ec)
            sLog.Error("      ImageServer", "Unable to create image directory %s: %s", subdir.c_str(), ec.message().c_str());
    }
    sLog.Blue("      ImageServer", "Image Server Initalized.");
}

void ImageServer::ReportNewImage(uint32 accountID, std::shared_ptr<std::vector<char> > imageData)
{
    sLog.Warning("      ImageServer"," ReportNewImage() called.");
    Lock lock(_limboLock);

    // the latest upload wins (a re-shot portrait replaces the pending one)
    _limboImages[accountID] = imageData;
}

bool ImageServer::SavePortrait(uint32 accountID, uint32 characterID)
{
    sLog.Warning("      ImageServer"," SavePortrait() called.");
    std::shared_ptr<std::vector<char> > data;
    {
        Lock lock(_limboLock);
        // check if we received an image from this account previously
        auto itr = _limboImages.find(accountID);
        if (itr == _limboImages.end()) {
            sLog.Error("      ImageServer"," Image not received for characterID %u.", characterID);
            return false;
        }
        data = itr->second;
        // and delete it from our limbo map
        _limboImages.erase(itr);
    }

    if ((data.get() == nullptr) or data->empty()) {
        sLog.Error("      ImageServer"," Empty image received for characterID %u.", characterID);
        return false;
    }

    /** @todo  we will need to make size 64 and size 40 images, and possibably 128/256 of char portaits.  GetImage() serves the 512 one for every size */
    // github.com/nothings/stb/blob/master/stb_image_resize.h
    // github.com/nothings/stb/blob/master/stb_image.h
    std::string dirName = "Character";
    std::string path(GetFilePath(dirName, characterID, 512));
    std::string tmpPath(path + ".tmp");

    // write a temp file, then move it over the old picture so a reader never sees a partial file
    FILE* fp = fopen(tmpPath.c_str(), "wb");
    if (fp == nullptr) {
        sLog.Error("      ImageServer", "Unable to open %s for writing: %s", tmpPath.c_str(), strerror(errno));
        return false;
    }
    size_t written = fwrite(data->data(), 1, data->size(), fp);
    bool ok = (fclose(fp) == 0) and (written == data->size());
    if (!ok) {
        sLog.Error("      ImageServer", "Unable to write %s (%zu of %zu bytes)", tmpPath.c_str(), written, data->size());
        std::error_code ec;
        std::filesystem::remove(tmpPath, ec);
        return false;
    }

    std::error_code ec;
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(tmpPath, path, ec);
    if (ec) {
        sLog.Error("      ImageServer", "Unable to move %s to %s: %s", tmpPath.c_str(), path.c_str(), ec.message().c_str());
        std::filesystem::remove(tmpPath, ec);
        return false;
    }

    sLog.Green("      ImageServer", "Received image from %u and saved as %s", accountID, path.c_str());
    return true;
}

std::shared_ptr<std::vector<char> > ImageServer::ReadFile(const std::string& path)
{
    FILE* fp = fopen(path.c_str(), "rb");
    if (fp == nullptr)
        return std::shared_ptr<std::vector<char> >();

    std::shared_ptr<std::vector<char> > ret;
    if (fseek(fp, 0, SEEK_END) == 0) {
        long length = ftell(fp);
        if ((length > 0) and (fseek(fp, 0, SEEK_SET) == 0)) {
            ret = std::shared_ptr<std::vector<char> >(new std::vector<char>());
            ret->resize(length);
            if (fread(ret->data(), 1, length, fp) != (size_t)length)
                ret.reset();
        }
    }

    fclose(fp);
    return ret;
}

std::shared_ptr<std::vector<char> > ImageServer::GetImage(std::string& category, uint32 id, uint32 size)
{
    // every portrait and icon the client or a killboard page asks for comes through here
    _log(IMAGE__TRACE, "GetImage() called. Cat: %s, id: %u, size:%u", category.c_str(), id, size);

    if (!ValidateCategory(category) || !ValidateSize(category, size))
        return std::shared_ptr<std::vector<char> >();

    std::shared_ptr<std::vector<char> > ret = ReadFile(GetFilePath(category, id, size));
    if (ret)
        return ret;

    // the requested size is missing.  portraits are only saved as 512 (the client scales them), other images use the largest one we have
    if (category == "Character")
        return ReadFile(GetFilePath(category, id, 512));

    static const uint32 sizes[] = { 1024, 512, 256, 128, 64, 40, 32 };
    for (auto cur : sizes) {
        if ((cur == size) or !ValidateSize(category, cur))
            continue;
        ret = ReadFile(GetFilePath(category, id, cur));
        if (ret)
            return ret;
    }

    return std::shared_ptr<std::vector<char> >();
}

std::string ImageServer::GetFilePath(std::string& category, uint32 id, uint32 size)
{
    std::string extension = category == "Character" ? "jpg" : "png";

    std::stringstream builder;
    builder << _basePath << category << "/" << id << "_" << size << "." << extension;
    return builder.str();
}

bool ImageServer::ValidateSize(std::string& category, uint32 size)
{
    if (category == "InventoryType")
        return size == 64 || size == 32;

    if (category == "Alliance")
        return size == 256 || size == 128 || size == 64 || size == 32;

    if (category == "Corporation")
        return size == 256 || size == 128 || size == 64 || size == 32;

    // Render and Character
    return size == 1024 || size == 512 || size == 256 || size == 128 || size == 64 || size == 40 || size == 32;
}

bool ImageServer::ValidateCategory(std::string& category)
{
    for (int i = 0; i < 5; i++)
        if (category == Categories[i])
            return true;
    return false;
}

std::string ImageServer::url(uint32 localIP)
{
    if (!_autoHost or (localIP == 0))
        return _url;

    // localIP is in network byte order, so its bytes are already in dotted order
    const uint8* ip = reinterpret_cast<const uint8*>(&localIP);
    std::stringstream urlBuilder;
    urlBuilder << "http://" << (uint32)ip[0] << "." << (uint32)ip[1] << "." << (uint32)ip[2] << "." << (uint32)ip[3];
    urlBuilder << ":" << sConfig.net.imageServerPort << "/";
    return urlBuilder.str();
}

void ImageServer::Run()
{
    _ioThread = std::shared_ptr<boost::asio::detail::thread>(new boost::asio::detail::thread(std::bind(&ImageServer::RunInternal, this)));
}

void ImageServer::Stop()
{
    _io->stop();
    _ioThread->join();
}

void ImageServer::RunInternal()
{
    _io = std::shared_ptr<boost::asio::io_context>(new boost::asio::io_context());
    _listener = std::shared_ptr<ImageServerListener>(new ImageServerListener(*_io));
    _io->run();
}

ImageServer::Lock::Lock(boost::asio::detail::mutex& mutex)
    : _mutex(mutex)
{
    _mutex.lock();
}

ImageServer::Lock::~Lock()
{
    _mutex.unlock();
}
