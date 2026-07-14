#pragma once
// ============================================================================
//  Dilgogram — общий протокол клиент/сервер
//  Подключается и в client.cpp, и в server.cpp, чтобы типы пакетов
//  и формат сериализации кириллицы НИКОГДА не расходились между файлами.
//  (В исходной версии это было продублировано вручную в двух местах —
//   классическая причина бага "сервер не понимает клиента после правки".)
// ============================================================================

#include <SFML/Network.hpp>
#include <string>

// Типы пакетов.
// ВАЖНО: значения — это просто порядковые номера enum (int32 на проводе).
// Никогда не меняй порядок существующих значений, только добавляй новые в конец,
// иначе старый клиент/сервер перестанет понимать пакеты друг друга.
enum PacketType {
    REGISTRATION = 0,   // клиент -> сервер: {nickname}
    TEXT_MSG,            // клиент -> сервер (broadcast): {nickname, text}
    IMAGE_MSG,            // клиент -> сервер (broadcast): {nickname, filename, bytes}
    VIDEO_MSG,            // клиент -> сервер (broadcast): {nickname, filename, bytes}
    CALL_REQUEST,         // клиент -> сервер (адресный): {targetNickname, fromNickname}
    CALL_ACCEPT,          // адресный: {targetNickname, fromNickname}
    CALL_DECLINE,         // адресный: {targetNickname, fromNickname}
    CALL_END,             // адресный: {targetNickname, fromNickname}
    CALL_AUDIO,           // адресный: {targetNickname, fromNickname, pcmBytes}
    USER_JOINED,          // сервер -> клиент: {nickname}
    USER_LEFT             // сервер -> клиент: {nickname}
};

// Пакеты типа CALL_* адресные — сервер не рассылает их всем, а пересылает
// только конкретному получателю (см. server.cpp). Функция помогает не
// забыть где-нибудь в коде обновить условие при добавлении нового типа звонка.
inline bool isCallPacket(PacketType t) {
    return t == CALL_REQUEST || t == CALL_ACCEPT || t == CALL_DECLINE ||
        t == CALL_END || t == CALL_AUDIO;
}

// ----------------------------------------------------------------------------
// Чтение/запись кириллицы (и вообще любого юникода) через UTF-32.
// Объявлены как `inline`, т.к. хедер подключается в двух .cpp файлах —
// без inline будет ошибка линковки "multiple definition of operator<<".
// ----------------------------------------------------------------------------
inline sf::Packet& operator >>(sf::Packet& packet, std::basic_string<sf::Uint32>& str) {
    sf::Uint32 size = 0;
    if (packet >> size) {
        str.clear();
        str.reserve(size);
        for (sf::Uint32 i = 0; i < size; ++i) {
            sf::Uint32 character = 0;
            packet >> character;
            str += character;
        }
    }
    return packet;
}

inline sf::Packet& operator <<(sf::Packet& packet, const std::basic_string<sf::Uint32>& str) {
    packet << static_cast<sf::Uint32>(str.size());
    for (sf::Uint32 character : str) {
        packet << character;
    }
    return packet;
}

inline std::basic_string<sf::Uint32> toU32(const std::wstring& w) {
    return sf::String(w).toUtf32();
}

inline std::wstring toWide(const std::basic_string<sf::Uint32>& u) {
    return sf::String(u).toWideString();
}


