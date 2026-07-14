#if defined(_DEBUG)
#pragma comment(lib, "C:/SFML/LIB/sfml-network-d.lib")
#pragma comment(lib, "C:/SFML/LIB/sfml-system-d.lib")
#else
#pragma comment(lib, "C:/SFML/LIB/sfml-network.lib")
#pragma comment(lib, "C:/SFML/LIB/sfml-system.lib")
#endif

#include <SFML/Network.hpp>
#include <windows.h>
#include <iostream>
#include <list>
#include <string>
#include "protocol.h"

// См. main.cpp клиента — та же проблема с кракозябрами и то же решение:
// UTF-8 узкие литералы + MultiByteToWideChar, вывод через WriteConsoleW.
std::wstring U(const char* utf8) {
    if (!utf8) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (len <= 0) return L"";
    std::wstring result(static_cast<size_t>(len - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &result[0], len);
    return result;
}

void consoleWriteLine(const std::wstring& text) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE || h == nullptr) return;
    std::wstring line = text + L"\r\n";
    DWORD written = 0;
    WriteConsoleW(h, line.c_str(), static_cast<DWORD>(line.size()), &written, nullptr);
}

struct User {
    sf::TcpSocket* socket;
    std::wstring nickname;
};

int main() {
    setlocale(LC_ALL, "Russian");

    sf::TcpListener listener;
    if (listener.listen(53000) != sf::Socket::Done) {
        consoleWriteLine(U("Ошибка запуска сервера FEM CLUB!"));
        return -1;
    }
    consoleWriteLine(U("Сервер FEM CLUB успешно запущен на порту 53000..."));

    std::list<User> users;
    sf::SocketSelector selector;
    selector.add(listener);

    auto sendPacket = [](User& u, sf::Packet& p) {
        u.socket->setBlocking(true);
        u.socket->send(p);
        };

    auto findUserByNick = [&](const std::wstring& nick) -> User* {
        for (auto& u : users) {
            if (u.nickname == nick) return &u;
        }
        return nullptr;
        };

    while (true) {
        if (selector.wait()) {
            if (selector.isReady(listener)) {
                sf::TcpSocket* clientSocket = new sf::TcpSocket;
                if (listener.accept(*clientSocket) == sf::Socket::Done) {
                    selector.add(*clientSocket);
                    users.push_back({ clientSocket, U("Аноним") });
                    consoleWriteLine(U("Новое сетевое подключение."));
                }
                else {
                    delete clientSocket;
                }
            }
            else {
                for (auto it = users.begin(); it != users.end();) {
                    if (!selector.isReady(*it->socket)) {
                        ++it;
                        continue;
                    }

                    sf::Packet packet;
                    sf::Socket::Status status = it->socket->receive(packet);

                    if (status == sf::Socket::Done) {
                        sf::Packet forward = packet;

                        sf::Int32 typeVal = -1;
                        packet >> typeVal;
                        PacketType type = static_cast<PacketType>(typeVal);

                        if (type == REGISTRATION) {
                            std::basic_string<sf::Uint32> rawNick;
                            packet >> rawNick;
                            it->nickname = toWide(rawNick);
                            consoleWriteLine(U("Пользователь зарегистрирован: ") + it->nickname);

                            for (auto& u : users) {
                                if (u.socket != it->socket && u.nickname != U("Аноним")) {
                                    sf::Packet joinInfo;
                                    joinInfo << static_cast<sf::Int32>(USER_JOINED) << toU32(u.nickname);
                                    sendPacket(*it, joinInfo);
                                }
                            }
                            sf::Packet joinBroadcast;
                            joinBroadcast << static_cast<sf::Int32>(USER_JOINED) << toU32(it->nickname);
                            for (auto& u : users) {
                                if (u.socket != it->socket) sendPacket(u, joinBroadcast);
                            }
                        }
                        else if (type == TEXT_MSG || type == IMAGE_MSG || type == VIDEO_MSG) {
                            for (auto& u : users) {
                                if (u.socket != it->socket) sendPacket(u, forward);
                            }
                        }
                        else if (isCallPacket(type)) {
                            std::basic_string<sf::Uint32> rawTarget;
                            packet >> rawTarget;
                            std::wstring targetNick = toWide(rawTarget);

                            User* target = findUserByNick(targetNick);
                            if (target) {
                                sendPacket(*target, forward);
                            }
                            else if (type == CALL_REQUEST) {
                                sf::Packet declineInfo;
                                declineInfo << static_cast<sf::Int32>(CALL_DECLINE) << toU32(it->nickname) << toU32(targetNick);
                                sendPacket(*it, declineInfo);
                            }
                        }

                        ++it;
                    }
                    else if (status == sf::Socket::Disconnected) {
                        consoleWriteLine(U("Пользователь ") + it->nickname + U(" покинул чат."));

                        sf::Packet leftBroadcast;
                        leftBroadcast << static_cast<sf::Int32>(USER_LEFT) << toU32(it->nickname);

                        selector.remove(*it->socket);
                        sf::TcpSocket* leavingSocket = it->socket;
                        it = users.erase(it);

                        for (auto& u : users) sendPacket(u, leftBroadcast);
                        delete leavingSocket;
                    }
                    else {
                        ++it;
                    }
                }
            }
        }
    }

    return 0;
}
