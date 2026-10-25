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

#include "imageserver/ImageServerConnection.h"
#include "killboard/Killboard.h"

boost::asio::const_buffer ImageServerConnection::_responseOK = boost::asio::buffer("HTTP/1.0 200 OK\r\nContent-Type: image/jpeg\r\n\r\n", 45);
boost::asio::const_buffer ImageServerConnection::_responseOKPng = boost::asio::buffer("HTTP/1.0 200 OK\r\nContent-Type: image/png\r\n\r\n", 44);
boost::asio::const_buffer ImageServerConnection::_responseNotFound = boost::asio::buffer("HTTP/1.0 404 Not Found\r\n\r\n", 26);
boost::asio::const_buffer ImageServerConnection::_responseRedirectBegin = boost::asio::buffer("HTTP/1.0 301 Moved Permanently\r\nLocation: ", 42);
boost::asio::const_buffer ImageServerConnection::_responseRedirectEnd = boost::asio::buffer("\r\n\r\n", 4);

ImageServerConnection::ImageServerConnection(boost::asio::io_context& io)
: _socket(io),
_id(0),
_size(0)
{
}

boost::asio::ip::tcp::socket& ImageServerConnection::socket()
{
    return _socket;
}

void ImageServerConnection::Process()
{
    // receive all HTTP headers from the client
    boost::asio::async_read_until(_socket, _buffer, "\r\n\r\n",
            std::bind(&ImageServerConnection::ProcessHeaders, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
}

void ImageServerConnection::ProcessHeaders(const boost::system::error_code& ec, std::size_t bytes)
{
    // disconnects and oversized headers (not_found) just close the connection
    if (ec) {
        Close();
        return;
    }

    std::istream stream(&_buffer);
    std::string request;

    // every request line ends with \r\n
    std::getline(stream, request, '\r');

    if (!starts_with(request, "GET /"))
    {
        NotFound();
        return;
    }
    request = request.substr(5);

    // the killboard's pages
    if ((request.compare(0, 2, "kb") == 0) and ((request.size() == 2) or (request[2] == '/') or (request[2] == '?') or (request[2] == ' '))) {
        ServeKillboard(request.substr(2), stream);
        return;
    }

    bool found = false;
    for (uint32 i = 0; i < ImageServer::CategoryCount; i++)
    {
        if (starts_with(request, ImageServer::Categories[i]))
        {
            found = true;
            _category = ImageServer::Categories[i];
            request = request.substr(strlen(ImageServer::Categories[i]));
            break;
        }
    }
    if (!found)
    {
        NotFound();
        return;
    }

    if (!starts_with(request, "/"))
    {
        NotFound();
        return;
    }
    request = request.substr(1);

    size_t del = request.find_first_of('_');
    if (del == std::string::npos)
    {
        NotFound();
        return;
    }

    // might have some extra data but atoi shouldn't care
    std::string idStr = request.substr(0, del);
    std::string sizeStr = request.substr(del + 1);
    _id = atoi(idStr.c_str());
    _size = atoi(sizeStr.c_str());

    _imageData = sImageServer.GetImage(_category, _id, _size);
    // the killboard's pages ask with a query (Character/1_32.jpg?kb):  they want a 404 rather than the fallback redirect
    if (!_imageData and (request.find('?') != std::string::npos)) {
        NotFound();
        return;
    }
    if (!_imageData) {
        if (IsPlayerItem(_id)) {
            sLog.Error("     Image Server","Image for itemID %u not found.", _id);
            NotFound();
            return;
        } else if (IsCharacterID(_id)) {
            sLog.Error("     Image Server","Image for charID %u not found.", _id);
            NotFound();
            return;
        }
        Redirect();
        return;
    }

    // first we have to send the responseOK, then our actual result.  portraits are jpg, logos and type icons png
    boost::asio::async_write(_socket, (_category == "Character" ? _responseOK : _responseOKPng), boost::asio::transfer_all(), std::bind(&ImageServerConnection::SendImage, shared_from_this()));
}

static std::string UrlDecode(const std::string& in)
{
    std::string out;
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '+') {
            out += ' ';
        } else if ((in[i] == '%') and ((i + 2) < in.size()) and isxdigit(in[i + 1]) and isxdigit(in[i + 2])) {
            out += (char)strtol(in.substr(i + 1, 2).c_str(), nullptr, 16);
            i += 2;
        } else {
            out += in[i];
        }
    }
    return out;
}

// request is the rest of the request line after "GET /kb" (path, query and the http version), stream the headers
void ImageServerConnection::ServeKillboard(const std::string& request, std::istream& stream)
{
    KbRequest req;
    std::string target(request.substr(0, request.find(' ')));
    size_t q(target.find('?'));
    req.path = UrlDecode(target.substr(0, q));
    if (req.path.empty())
        req.path = "/";
    if (q != std::string::npos) {
        std::string query(target.substr(q + 1));
        size_t pos(0);
        while (pos < query.size()) {
            size_t amp(query.find('&', pos));
            if (amp == std::string::npos)
                amp = query.size();
            std::string pair(query.substr(pos, amp - pos));
            size_t eq(pair.find('='));
            if (eq == std::string::npos)
                req.query[UrlDecode(pair)] = std::string();
            else
                req.query[UrlDecode(pair.substr(0, eq))] = UrlDecode(pair.substr(eq + 1));
            pos = amp + 1;
        }
    }
    // the in-game browser sends EVE_CHARID, EVE_TRUSTED... on a trusted site
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() and (line.back() == '\r'))
            line.pop_back();
        if (line.empty())
            continue;
        size_t colon(line.find(':'));
        if (colon == std::string::npos)
            continue;
        std::string name(line.substr(0, colon));
        for (auto& c : name)
            c = (c == '-' ? '_' : (char)tolower(c));
        std::string value(line.substr(colon + 1));
        value.erase(0, value.find_first_not_of(" \t"));
        req.headers[name] = value;
    }

    KbResponse rsp;
    sKillboard.Handle(req, rsp);
    const char* status(rsp.status == 200 ? "OK" : (rsp.status == 404 ? "Not Found" : "Service Unavailable"));
    std::stringstream head;
    head << "HTTP/1.0 " << rsp.status << " " << status << "\r\n"
         << "Content-Type: " << rsp.contentType << "\r\n"
         << "Content-Length: " << rsp.body.size() << "\r\n"
         << "Cache-Control: no-cache\r\n"
         << "Connection: close\r\n\r\n";
    _response = head.str() + rsp.body;
    boost::asio::async_write(_socket, boost::asio::buffer(_response), boost::asio::transfer_all(), std::bind(&ImageServerConnection::Close, shared_from_this()));
}

void ImageServerConnection::SendImage()
{
    boost::asio::async_write(_socket, boost::asio::buffer(*_imageData, _imageData->size()), boost::asio::transfer_all(), std::bind(&ImageServerConnection::Close, shared_from_this()));
}

void ImageServerConnection::NotFound()
{
    boost::asio::async_write(_socket, _responseNotFound, boost::asio::transfer_all(), std::bind(&ImageServerConnection::Close, shared_from_this()));
}

void ImageServerConnection::Redirect()
{
    boost::asio::async_write(_socket, _responseRedirectBegin, boost::asio::transfer_all(), std::bind(&ImageServerConnection::RedirectLocation, shared_from_this()));
}

void ImageServerConnection::RedirectLocation()
{
    sLog.Error("     Image Server"," RedirectLocation() called.");
    std::string extension = _category == "Character" ? "jpg" : "png";
    std::stringstream url;
    url << ImageServer::FallbackURL << _category << "/" << _id << "_" << _size << "." << extension;
    _redirectUrl = url.str();
    boost::asio::async_write(_socket, boost::asio::buffer(_redirectUrl), boost::asio::transfer_all(), std::bind(&ImageServerConnection::RedirectFinalize, shared_from_this()));
}

void ImageServerConnection::RedirectFinalize()
{
    boost::asio::async_write(_socket, _responseRedirectEnd, boost::asio::transfer_all(), std::bind(&ImageServerConnection::Close, shared_from_this()));
}

void ImageServerConnection::Close()
{
    boost::system::error_code ec;
    _socket.close(ec);
}

bool ImageServerConnection::starts_with(std::string& haystack, const char *const needle)
{
    return haystack.substr(0, strlen(needle)).compare(needle) == 0;
}

std::shared_ptr<ImageServerConnection> ImageServerConnection::create(boost::asio::io_context& io)
{
    return std::shared_ptr<ImageServerConnection>(new ImageServerConnection(io));
}
