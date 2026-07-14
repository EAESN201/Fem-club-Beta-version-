
#if defined(_DEBUG)
#pragma comment(lib, "C:/SFML/LIB/sfml-graphics-d.lib")
#pragma comment(lib, "C:/SFML/LIB/sfml-window-d.lib")
#pragma comment(lib, "C:/SFML/LIB/sfml-network-d.lib")
#pragma comment(lib, "C:/SFML/LIB/sfml-audio-d.lib")
#pragma comment(lib, "C:/SFML/LIB/sfml-system-d.lib")
#else
#pragma comment(lib, "C:/SFML/LIB/sfml-graphics.lib")
#pragma comment(lib, "C:/SFML/LIB/sfml-window.lib")
#pragma comment(lib, "C:/SFML/LIB/sfml-network.lib")
#pragma comment(lib, "C:/SFML/LIB/sfml-audio.lib")
#pragma comment(lib, "C:/SFML/LIB/sfml-system.lib")
#endif
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
 
#include <SFML/Graphics.hpp>
#include <SFML/Network.hpp>
#include <SFML/Audio.hpp>
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
 
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include <chrono>
#include <algorithm>
 
#include "protocol.h"
 
// ============================================================================
//  ОБЩИЕ ХЕЛПЕРЫ
// ============================================================================
 
static std::wstring g_serverIp = L"127.0.0.1";
static const unsigned short SERVER_PORT = 53000;
 
std::wstring U(const char* utf8) {
    if (!utf8) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (len <= 0) return L"";
    std::wstring result(static_cast<size_t>(len - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &result[0], len);
    return result;
}
 
// Безопасный вывод в консоль (в обход конфликта wcout/кодовых страниц).
void consoleWriteLine(const std::wstring& text) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE || h == nullptr) return;
    std::wstring line = text + L"\r\n";
    DWORD written = 0;
    WriteConsoleW(h, line.c_str(), static_cast<DWORD>(line.size()), &written, nullptr);
}
 
// Простое расширение ASCII-строки (для аргументов командной строки, ников в тестах)
std::wstring asciiWiden(const char* s) {
    std::wstring w;
    while (s && *s) w += static_cast<wchar_t>(static_cast<unsigned char>(*s++));
    return w;
}
 
std::wstring extractFileName(const std::wstring& path) {
    size_t pos = path.find_last_of(L"/\\");
    return (pos == std::wstring::npos) ? path : path.substr(pos + 1);
}
 
std::string readFileBinary(const std::wstring& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return "";
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}
 
bool writeFileBinary(const std::wstring& path, const std::string& data) {
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    file.write(data.data(), static_cast<std::streamsize>(data.size()));
    return true;
}
 
std::wstring nowTimestamp() {
    auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    tm local{};
    localtime_s(&local, &t);
    wchar_t buf[32];
    swprintf_s(buf, L"%02d%02d%02d_%02d%02d%02d", local.tm_year % 100, local.tm_mon + 1, local.tm_mday,
        local.tm_hour, local.tm_min, local.tm_sec);
    return buf;
}
 
// IP-адреса и хостнеймы — это ASCII, поэтому простое сужение wchar_t->char безопасно
std::string narrowAscii(const std::wstring& w) {
    std::string s;
    s.reserve(w.size());
    for (wchar_t c : w) s += static_cast<char>(c);
    return s;
}
 
// Диалог выбора файла (стандартный проводник Windows)
std::wstring openFileDialog(const wchar_t* filter) {
    wchar_t filename[MAX_PATH] = L"";
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) {
        return std::wstring(filename);
    }
    return L"";
}
 
// ============================================================================
//  ПЕРЕНОС ТЕКСТА ПО ШИРИНЕ
// ============================================================================
 
std::vector<std::wstring> wrapText(const std::wstring& text, sf::Font& font, unsigned charSize, float maxWidth) {
    std::vector<std::wstring> lines;
    sf::Text probe(L"", font, charSize);
 
    auto width = [&](const std::wstring& s) -> float {
        probe.setString(s);
        return probe.getLocalBounds().width;
        };
 
    std::vector<std::wstring> tokens;
    std::wstring cur;
    for (wchar_t c : text) {
        if (c == L' ') { tokens.push_back(cur + L" "); cur.clear(); }
        else if (c == L'\n') { tokens.push_back(cur); tokens.push_back(L"\n"); cur.clear(); }
        else cur += c;
    }
    tokens.push_back(cur);
 
    std::wstring line;
    for (auto token : tokens) {
        if (token == L"\n") {
            lines.push_back(line);
            line.clear();
            continue;
        }
        if (width(line + token) <= maxWidth) {
            line += token;
            continue;
        }
        if (!line.empty()) { lines.push_back(line); line.clear(); }
        while (width(token) > maxWidth && token.size() > 1) {
            size_t cut = token.size();
            while (cut > 1 && width(token.substr(0, cut)) > maxWidth) cut--;
            lines.push_back(token.substr(0, cut));
            token = token.substr(cut);
        }
        line = token;
    }
    if (!line.empty() || lines.empty()) lines.push_back(line);
    return lines;
}
 
// ============================================================================
//  РИСОВАНИЕ СКРУГЛЁННЫХ ПРЯМОУГОЛЬНИКОВ (пузыри, кнопки, поля)
// ============================================================================
 
void drawRoundedRect(sf::RenderWindow& win, float x, float y, float w, float h, float radius, sf::Color color) {
    radius = std::min(radius, std::min(w, h) / 2.f);
    if (radius < 0.5f) {
        sf::RectangleShape r({ w, h });
        r.setPosition(x, y);
        r.setFillColor(color);
        win.draw(r);
        return;
    }
 
    sf::RectangleShape rectH({ w - 2.f * radius, h });
    rectH.setPosition(x + radius, y);
    rectH.setFillColor(color);
    win.draw(rectH);
 
    sf::RectangleShape rectV({ w, h - 2.f * radius });
    rectV.setPosition(x, y + radius);
    rectV.setFillColor(color);
    win.draw(rectV);
 
    sf::CircleShape corner(radius);
    corner.setFillColor(color);
    corner.setPosition(x, y);                                   win.draw(corner);
    corner.setPosition(x + w - 2.f * radius, y);                win.draw(corner);
    corner.setPosition(x, y + h - 2.f * radius);                win.draw(corner);
    corner.setPosition(x + w - 2.f * radius, y + h - 2.f * radius); win.draw(corner);
}
 
// Детерминированный цвет по нику — чтобы у каждого собеседника был свой акцент.
sf::Color colorForName(const std::wstring& name) {
    static const sf::Color palette[] = {
        sf::Color(255, 110, 180),
        sf::Color(120, 170, 255),
        sf::Color(120, 220, 170),
        sf::Color(255, 190, 90),
        sf::Color(190, 140, 255),
        sf::Color(255, 140, 120),
    };
    std::size_t h = std::hash<std::wstring>{}(name);
    return palette[h % 6];
}
 
// ============================================================================
//  СООБЩЕНИЯ ЧАТА
// ============================================================================
 
enum class MsgKind { SYSTEM, TEXT, IMAGE, VIDEO };
 
struct ChatMessage {
    MsgKind kind;
    std::wstring author;
    std::wstring text;
    std::wstring filePath;
};
 
// ============================================================================
//  ОЧЕРЕДЬ ВХОДЯЩИХ СОБЫТИЙ (сеть -> UI, потокобезопасно)
// ============================================================================
 
struct IncomingEvent {
    PacketType type;
    std::wstring nickname;
    std::wstring text;
    std::wstring filename;
    std::string  bytes;
};
 
class EventQueue {
public:
    void push(IncomingEvent e) {
        std::lock_guard<std::mutex> lock(mtx);
        q.push_back(std::move(e));
    }
    bool pop(IncomingEvent& out) {
        std::lock_guard<std::mutex> lock(mtx);
        if (q.empty()) return false;
        out = std::move(q.front());
        q.pop_front();
        return true;
    }
private:
    std::deque<IncomingEvent> q;
    std::mutex mtx;
};
 
// ============================================================================
//  ГОЛОСОВЫЕ ЗВОНКИ
// ============================================================================
 
class NetworkRecorder : public sf::SoundRecorder {
public:
    explicit NetworkRecorder(std::function<void(const sf::Int16*, std::size_t)> onChunk)
        : callback(std::move(onChunk)) {
        setProcessingInterval(sf::milliseconds(100));
    }
protected:
    bool onProcessSamples(const sf::Int16* samples, std::size_t sampleCount) override {
        if (callback) callback(samples, sampleCount);
        return running.load();
    }
public:
    std::atomic<bool> running{ true };
private:
    std::function<void(const sf::Int16*, std::size_t)> callback;
};
 
class NetworkAudioStream : public sf::SoundStream {
public:
    NetworkAudioStream() { initialize(1, 16000); }
 
    void pushSamples(const sf::Int16* data, std::size_t count) {
        std::lock_guard<std::mutex> lock(mtx);
        buffer.insert(buffer.end(), data, data + count);
        if (buffer.size() > 16000 * 5) {
            buffer.erase(buffer.begin(), buffer.begin() + (buffer.size() - 16000 * 2));
        }
    }
    void clear() {
        std::lock_guard<std::mutex> lock(mtx);
        buffer.clear();
    }
protected:
    bool onGetData(Chunk& data) override {
        std::lock_guard<std::mutex> lock(mtx);
        const std::size_t chunkSize = 1600;
        if (buffer.size() < chunkSize) {
            silence.assign(chunkSize, 0);
            data.samples = silence.data();
            data.sampleCount = silence.size();
            return true;
        }
        playChunk.assign(buffer.begin(), buffer.begin() + chunkSize);
        buffer.erase(buffer.begin(), buffer.begin() + chunkSize);
        data.samples = playChunk.data();
        data.sampleCount = playChunk.size();
        return true;
    }
    void onSeek(sf::Time) override {}
private:
    std::mutex mtx;
    std::vector<sf::Int16> buffer;
    std::vector<sf::Int16> playChunk;
    std::vector<sf::Int16> silence;
};
 
// ============================================================================
//  КНОПКА (со скруглёнными углами)
// ============================================================================
 
struct Button {
    sf::FloatRect rect;
    std::wstring label;
    bool hovered = false;
    bool enabled = true;
 
    bool contains(sf::Vector2f p) const { return enabled && rect.contains(p); }
 
    void draw(sf::RenderWindow& win, sf::Font& font, sf::Color base, sf::Color hoverColor) {
        sf::Color fill = enabled ? (hovered ? hoverColor : base) : sf::Color(60, 62, 70);
        drawRoundedRect(win, rect.left, rect.top, rect.width, rect.height, 10.f, fill);
 
        sf::Text t(label, font, 16);
        t.setFillColor(enabled ? sf::Color::White : sf::Color(150, 150, 155));
        sf::FloatRect tb = t.getLocalBounds();
        t.setPosition(rect.left + (rect.width - tb.width) / 2.f - tb.left,
            rect.top + (rect.height - tb.height) / 2.f - tb.top - 1.f);
        win.draw(t);
    }
};
 
// ============================================================================
//  ГЛОБАЛЬНОЕ СОСТОЯНИЕ ЗВОНКА
// ============================================================================
 
enum class CallState { NONE, OUTGOING, INCOMING, ACTIVE };
 
// ============================================================================
//  ЦВЕТОВАЯ ТЕМА FEM CLUB
// ============================================================================
 
namespace Theme {
    const sf::Color bg(20, 21, 25);
    const sf::Color sidebarBg(16, 17, 21);
    const sf::Color panelBg(30, 31, 37);
    const sf::Color inputBg(38, 39, 46);
    const sf::Color accent(70, 110, 160);       // нейтральный синевато-серый акцент
    const sf::Color accentDark(55, 90, 135);
    const sf::Color accentSoft(70, 110, 160, 60);
    const sf::Color bubbleMine(38, 62, 92);      // тёмно-синий, как на референсе
    const sf::Color bubbleOther(45, 47, 55);
    const sf::Color bubbleSystem(50, 52, 60);
    const sf::Color textMain(235, 236, 240);
    const sf::Color textDim(140, 142, 150);
}
 
int main(int argc, char** argv) {
    setlocale(LC_ALL, "Russian");
 
    std::wstring presetNick, presetIp;
    if (argc > 1) presetNick = asciiWiden(argv[1]);
    if (argc > 2) presetIp = asciiWiden(argv[2]);
 
    sf::Font font;
    bool fontLoaded = font.loadFromFile("arial.ttf")
        || font.loadFromFile("C:/Windows/Fonts/arial.ttf")
        || font.loadFromFile("C:/Windows/Fonts/segoeui.ttf")
        || font.loadFromFile("C:/Windows/Fonts/tahoma.ttf")
        || font.loadFromFile("C:/Windows/Fonts/calibri.ttf");
 
    if (!fontLoaded) {
        consoleWriteLine(U("Не найден шрифт с поддержкой кириллицы (arial.ttf / segoeui.ttf / tahoma.ttf). "
            "Положи любой .ttf рядом с программой под именем arial.ttf."));
        return -1;
    }
 
    sf::RenderWindow window(sf::VideoMode(960, 640), L"FEM CLUB");
    window.setFramerateLimit(60);
 
    // Если запущено с аргументом ника — заголовок окна помечаем, удобно при
    // тестировании нескольких запущенных копий одновременно.
    if (!presetNick.empty()) {
        window.setTitle(L"FEM CLUB — " + presetNick);
    }
 
    // ------------------------------------------------------------------
    // Состояние "экрана входа"
    // ------------------------------------------------------------------
    enum class AppState { LOGIN, CHAT };
    AppState appState = AppState::LOGIN;
 
    std::wstring ipInput = presetIp.empty() ? g_serverIp : presetIp;
    std::wstring nickInput = presetNick;
    int loginFocus = presetNick.empty() ? 1 : 0;
    std::wstring loginError;
 
    // ------------------------------------------------------------------
    // Сеть
    // ------------------------------------------------------------------
    sf::TcpSocket socket;
    std::mutex socketMutex;
    std::atomic<bool> connected{ false };
    std::atomic<bool> netRunning{ false };
    std::thread netThread;
    EventQueue incoming;
    std::wstring myNick;
 
    auto safeSend = [&](sf::Packet p) -> bool {
        std::lock_guard<std::mutex> lock(socketMutex);
        socket.setBlocking(true);
        return socket.send(p) == sf::Socket::Done;
        };
 
    // ------------------------------------------------------------------
    // Данные чата
    // ------------------------------------------------------------------
    std::vector<ChatMessage> messages;
    std::vector<std::wstring> onlineUsers;
    std::wstring selectedUser;
 
    std::wstring messageInput;
    float chatScroll = 0.f;
 
    struct FileClickRect { sf::FloatRect rect; std::wstring path; };
    std::vector<FileClickRect> fileClickRects;
 
    // ------------------------------------------------------------------
    // Звонки
    // ------------------------------------------------------------------
    CallState callState = CallState::NONE;
    std::wstring callPeer;
    std::unique_ptr<NetworkRecorder> recorder;
    NetworkAudioStream playback;
 
    auto stopCallAudio = [&]() {
        if (recorder) {
            recorder->running = false;
            recorder->stop();
            recorder.reset();
        }
        if (playback.getStatus() != sf::SoundStream::Stopped) playback.stop();
        playback.clear();
        };
 
    auto startCallAudio = [&]() {
        if (!sf::SoundRecorder::isAvailable()) {
            messages.push_back({ MsgKind::SYSTEM, L"", U("Микрофон недоступен на этом устройстве."), L"" });
            return;
        }
        recorder = std::make_unique<NetworkRecorder>([&](const sf::Int16* samples, std::size_t count) {
            if (callState != CallState::ACTIVE) return;
            std::string bytes(reinterpret_cast<const char*>(samples), count * sizeof(sf::Int16));
            sf::Packet p;
            p << static_cast<sf::Int32>(CALL_AUDIO) << toU32(callPeer) << toU32(myNick) << bytes;
            safeSend(p);
            });
        recorder->start(16000);
        playback.play();
        };
 
    // ------------------------------------------------------------------
    // Приём пакетов в отдельном потоке
    // ------------------------------------------------------------------
    auto receiveLoop = [&]() {
        socket.setBlocking(true);
        while (netRunning) {
            sf::Packet packet;
            sf::Socket::Status status = socket.receive(packet);
            if (status == sf::Socket::Done) {
                sf::Int32 typeVal = -1;
                packet >> typeVal;
                PacketType type = static_cast<PacketType>(typeVal);
                IncomingEvent ev;
                ev.type = type;
 
                if (type == USER_JOINED || type == USER_LEFT) {
                    std::basic_string<sf::Uint32> rawNick;
                    packet >> rawNick;
                    ev.nickname = toWide(rawNick);
                }
                else if (type == TEXT_MSG) {
                    std::basic_string<sf::Uint32> rawNick, rawText;
                    packet >> rawNick >> rawText;
                    ev.nickname = toWide(rawNick);
                    ev.text = toWide(rawText);
                }
                else if (type == IMAGE_MSG || type == VIDEO_MSG) {
                    std::basic_string<sf::Uint32> rawNick, rawName;
                    packet >> rawNick >> rawName;
                    ev.nickname = toWide(rawNick);
                    ev.filename = toWide(rawName);
                    packet >> ev.bytes;
                }
                else if (isCallPacket(type)) {
                    std::basic_string<sf::Uint32> rawTarget, rawFrom;
                    packet >> rawTarget >> rawFrom;
                    ev.nickname = toWide(rawFrom);
                    if (type == CALL_AUDIO) {
                        packet >> ev.bytes;
                    }
                }
                incoming.push(std::move(ev));
            }
            else if (status == sf::Socket::Disconnected) {
                IncomingEvent ev;
                ev.type = static_cast<PacketType>(-1);
                incoming.push(std::move(ev));
                break;
            }
        }
        };
 
    // ------------------------------------------------------------------
    // Кнопки
    // ------------------------------------------------------------------
    Button loginButton{ {0,0,180,44}, U("Войти") };
    Button photoButton{ {0,0,100,38}, U("Фото") };
    Button videoButton{ {0,0,100,38}, U("Видео") };
    Button callButton{ {0,0,170,38}, U("Позвонить") };
    Button acceptButton{ {0,0,140,46}, U("Принять") };
    Button declineButton{ {0,0,140,46}, U("Отклонить") };
    Button hangupButton{ {0,0,170,42}, U("Завершить звонок") };
 
    const float SIDEBAR_W = 210.f;
    const float INPUT_H = 54.f;
    const float TOOLBAR_H = 50.f;
 
    while (window.isOpen()) {
        sf::Vector2f mouse(sf::Mouse::getPosition(window));
 
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed) {
                netRunning = false;
                if (connected) socket.disconnect();
                stopCallAudio();
                if (netThread.joinable()) netThread.join();
                window.close();
            }
 
            // ---------------- ЭКРАН ЛОГИНА ----------------
            if (appState == AppState::LOGIN) {
                if (event.type == sf::Event::MouseButtonPressed) {
                    sf::FloatRect ipBox(300, 250, 400, 42);
                    sf::FloatRect nickBox(300, 320, 400, 42);
                    if (ipBox.contains(mouse)) loginFocus = 0;
                    else if (nickBox.contains(mouse)) loginFocus = 1;
 
                    if (loginButton.contains(mouse) && !nickInput.empty() && !ipInput.empty()) {
                        socket.setBlocking(true);
                        sf::IpAddress addr(narrowAscii(ipInput));
                        if (socket.connect(addr, SERVER_PORT, sf::seconds(5)) == sf::Socket::Done) {
                            myNick = nickInput;
                            sf::Packet reg;
                            reg << static_cast<sf::Int32>(REGISTRATION) << toU32(myNick);
                            socket.send(reg);
 
                            connected = true;
                            netRunning = true;
                            netThread = std::thread(receiveLoop);
                            appState = AppState::CHAT;
                            loginError.clear();
                            window.setTitle(L"FEM CLUB — " + myNick);
                        }
                        else {
                            loginError = U("Не удалось подключиться к ") + ipInput + L":53000";
                        }
                    }
                }
                if (event.type == sf::Event::TextEntered) {
                    std::wstring& target = (loginFocus == 0) ? ipInput : nickInput;
                    if (event.text.unicode == 8) { if (!target.empty()) target.pop_back(); }
                    else if (event.text.unicode == 9) { loginFocus = 1 - loginFocus; }
                    else if (event.text.unicode >= 32 && target.size() < 40) {
                        target += static_cast<wchar_t>(event.text.unicode);
                    }
                }
                if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Enter) {
                    if (loginFocus == 1 && !nickInput.empty() && !ipInput.empty()) {
                        // Enter на поле ника — сразу пробуем войти
                        sf::Event fake;
                        (void)fake;
                    }
                    loginFocus = 1 - loginFocus;
                }
            }
            // ---------------- ЭКРАН ЧАТА ----------------
            else if (appState == AppState::CHAT) {
                if (event.type == sf::Event::TextEntered) {
                    if (event.text.unicode == 8) { if (!messageInput.empty()) messageInput.pop_back(); }
                    else if (event.text.unicode == 13 || event.text.unicode == 10) { /* обрабатывается ниже */ }
                    else if (event.text.unicode >= 32 && messageInput.size() < 1000) {
                        messageInput += static_cast<wchar_t>(event.text.unicode);
                    }
                }
                if (event.type == sf::Event::KeyPressed) {
                    if (event.key.code == sf::Keyboard::Enter && !messageInput.empty()) {
                        sf::Packet p;
                        p << static_cast<sf::Int32>(TEXT_MSG) << toU32(myNick) << toU32(messageInput);
                        safeSend(p);
                        messages.push_back({ MsgKind::TEXT, myNick, messageInput, L"" });
                        messageInput.clear();
                        chatScroll = 0.f;
                    }
                }
                if (event.type == sf::Event::MouseWheelScrolled) {
                    chatScroll -= event.mouseWheelScroll.delta * 25.f;
                    if (chatScroll < 0.f) chatScroll = 0.f;
                }
                if (event.type == sf::Event::MouseButtonPressed) {
                    for (size_t i = 0; i < onlineUsers.size(); ++i) {
                        sf::FloatRect row(6, 46.f + i * 38.f, SIDEBAR_W - 12, 34.f);
                        if (row.contains(mouse)) selectedUser = onlineUsers[i];
                    }
 
                    if (photoButton.contains(mouse)) {
                        std::wstring path = openFileDialog(L"Изображения\0*.png;*.jpg;*.jpeg;*.bmp;*.gif\0Все файлы\0*.*\0");
                        if (!path.empty()) {
                            std::string bytes = readFileBinary(path);
                            if (!bytes.empty()) {
                                std::wstring fname = extractFileName(path);
                                sf::Packet p;
                                p << static_cast<sf::Int32>(IMAGE_MSG) << toU32(myNick) << toU32(fname) << bytes;
                                if (safeSend(p)) {
                                    messages.push_back({ MsgKind::IMAGE, myNick, U("отправил(а) фото: ") + fname, path });
                                    chatScroll = 0.f;
                                }
                            }
                        }
                    }
                    if (videoButton.contains(mouse)) {
                        std::wstring path = openFileDialog(L"Видео\0*.mp4;*.avi;*.mkv;*.mov;*.wmv\0Все файлы\0*.*\0");
                        if (!path.empty()) {
                            std::string bytes = readFileBinary(path);
                            if (!bytes.empty()) {
                                std::wstring fname = extractFileName(path);
                                sf::Packet p;
                                p << static_cast<sf::Int32>(VIDEO_MSG) << toU32(myNick) << toU32(fname) << bytes;
                                if (safeSend(p)) {
                                    messages.push_back({ MsgKind::VIDEO, myNick, U("отправил(а) видео: ") + fname, path });
                                    chatScroll = 0.f;
                                }
                            }
                        }
                    }
                    if (callButton.contains(mouse) && callState == CallState::NONE && !selectedUser.empty()) {
                        callPeer = selectedUser;
                        sf::Packet p;
                        p << static_cast<sf::Int32>(CALL_REQUEST) << toU32(callPeer) << toU32(myNick);
                        safeSend(p);
                        callState = CallState::OUTGOING;
                    }
                    if (callState == CallState::INCOMING) {
                        if (acceptButton.contains(mouse)) {
                            sf::Packet p;
                            p << static_cast<sf::Int32>(CALL_ACCEPT) << toU32(callPeer) << toU32(myNick);
                            safeSend(p);
                            callState = CallState::ACTIVE;
                            startCallAudio();
                        }
                        if (declineButton.contains(mouse)) {
                            sf::Packet p;
                            p << static_cast<sf::Int32>(CALL_DECLINE) << toU32(callPeer) << toU32(myNick);
                            safeSend(p);
                            callState = CallState::NONE;
                        }
                    }
                    if (callState == CallState::ACTIVE && hangupButton.contains(mouse)) {
                        sf::Packet p;
                        p << static_cast<sf::Int32>(CALL_END) << toU32(callPeer) << toU32(myNick);
                        safeSend(p);
                        stopCallAudio();
                        callState = CallState::NONE;
                    }
 
                    for (auto& fc : fileClickRects) {
                        if (fc.rect.contains(mouse)) {
                            ShellExecuteW(nullptr, L"open", fc.path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                            break;
                        }
                    }
                }
            }
        }
 
        // --------------------------------------------------------------
        // Разбор входящих сетевых событий
        // --------------------------------------------------------------
        IncomingEvent ev;
        while (incoming.pop(ev)) {
            if (static_cast<int>(ev.type) == -1) {
                messages.push_back({ MsgKind::SYSTEM, L"", U("Соединение с сервером потеряно."), L"" });
                connected = false;
                continue;
            }
            switch (ev.type) {
            case USER_JOINED:
                if (std::find(onlineUsers.begin(), onlineUsers.end(), ev.nickname) == onlineUsers.end())
                    onlineUsers.push_back(ev.nickname);
                messages.push_back({ MsgKind::SYSTEM, L"", ev.nickname + U(" в сети"), L"" });
                break;
            case USER_LEFT:
                onlineUsers.erase(std::remove(onlineUsers.begin(), onlineUsers.end(), ev.nickname), onlineUsers.end());
                messages.push_back({ MsgKind::SYSTEM, L"", ev.nickname + U(" вышел(а)"), L"" });
                if (callPeer == ev.nickname && callState != CallState::NONE) {
                    stopCallAudio();
                    callState = CallState::NONE;
                }
                break;
            case TEXT_MSG:
                messages.push_back({ MsgKind::TEXT, ev.nickname, ev.text, L"" });
                chatScroll = 0.f;
                break;
            case IMAGE_MSG:
            case VIDEO_MSG: {
                std::wstring folder = (ev.type == IMAGE_MSG) ? L"received_images" : L"received_videos";
                CreateDirectoryW(folder.c_str(), nullptr);
                std::wstring savePath = folder + L"/" + nowTimestamp() + L"_" + ev.filename;
                writeFileBinary(savePath, ev.bytes);
                std::wstring caption = ev.nickname + U(" прислал(а) ") +
                    (ev.type == IMAGE_MSG ? U("фото: ") : U("видео: ")) + ev.filename +
                    U("  (сохранено, клик — открыть)");
                messages.push_back({ ev.type == IMAGE_MSG ? MsgKind::IMAGE : MsgKind::VIDEO, ev.nickname, caption, savePath });
                chatScroll = 0.f;
                break;
            }
            case CALL_REQUEST:
                if (callState == CallState::NONE) {
                    callPeer = ev.nickname;
                    callState = CallState::INCOMING;
                }
                else {
                    sf::Packet p;
                    p << static_cast<sf::Int32>(CALL_DECLINE) << toU32(ev.nickname) << toU32(myNick);
                    safeSend(p);
                }
                break;
            case CALL_ACCEPT:
                if (callState == CallState::OUTGOING && ev.nickname == callPeer) {
                    callState = CallState::ACTIVE;
                    startCallAudio();
                }
                break;
            case CALL_DECLINE:
                if ((callState == CallState::OUTGOING || callState == CallState::INCOMING) && ev.nickname == callPeer) {
                    messages.push_back({ MsgKind::SYSTEM, L"", callPeer + U(" отклонил(а) звонок"), L"" });
                    callState = CallState::NONE;
                }
                break;
            case CALL_END:
                if (callState == CallState::ACTIVE && ev.nickname == callPeer) {
                    stopCallAudio();
                    messages.push_back({ MsgKind::SYSTEM, L"", U("Звонок с ") + callPeer + U(" завершён"), L"" });
                    callState = CallState::NONE;
                }
                break;
            case CALL_AUDIO:
                if (callState == CallState::ACTIVE && ev.nickname == callPeer) {
                    const sf::Int16* samples = reinterpret_cast<const sf::Int16*>(ev.bytes.data());
                    std::size_t count = ev.bytes.size() / sizeof(sf::Int16);
                    playback.pushSamples(samples, count);
                }
                break;
            default: break;
            }
        }
 
        // --------------------------------------------------------------
        // ОТРИСОВКА
        // --------------------------------------------------------------
        window.clear(Theme::bg);
 
        if (appState == AppState::LOGIN) {
            // Декоративная акцентная полоса сверху
            sf::RectangleShape topBar({ static_cast<float>(window.getSize().x), 6.f });
            topBar.setFillColor(Theme::accent);
            window.draw(topBar);
 
            sf::Text title(L"FEM CLUB", font, 40);
            title.setStyle(sf::Text::Bold);
            title.setFillColor(sf::Color::White);
            title.setPosition(300, 130);
            window.draw(title);
 
            sf::Text subtitle(U("свой чат для своих"), font, 16);
            subtitle.setFillColor(Theme::textDim);
            subtitle.setPosition(302, 182);
            window.draw(subtitle);
 
            auto drawField = [&](sf::FloatRect box, const std::wstring& label, const std::wstring& value, bool focused) {
                drawRoundedRect(window, box.left, box.top, box.width, box.height, 10.f, Theme::inputBg);
                if (focused) {
                    sf::RectangleShape border({ box.width, 2.f });
                    border.setPosition(box.left, box.top + box.height - 2.f);
                    border.setFillColor(Theme::accent);
                    window.draw(border);
                }
 
                sf::Text lbl(label, font, 13);
                lbl.setFillColor(Theme::textDim);
                lbl.setPosition(box.left + 2.f, box.top - 20);
                window.draw(lbl);
 
                sf::Text val(value, font, 18);
                val.setFillColor(Theme::textMain);
                val.setPosition(box.left + 14, box.top + 10);
                window.draw(val);
                };
 
            sf::FloatRect ipBox(300, 250, 400, 42);
            sf::FloatRect nickBox(300, 320, 400, 42);
            drawField(ipBox, U("IP сервера"), ipInput, loginFocus == 0);
            drawField(nickBox, U("Никнейм"), nickInput, loginFocus == 1);
 
            loginButton.rect = { 300, 385, 180, 44 };
            loginButton.hovered = loginButton.contains(mouse);
            loginButton.enabled = !nickInput.empty() && !ipInput.empty();
            loginButton.draw(window, font, Theme::accent, sf::Color(255, 100, 175));
 
            if (!loginError.empty()) {
                sf::Text err(loginError, font, 15);
                err.setFillColor(sf::Color(255, 110, 110));
                err.setPosition(300, 445);
                window.draw(err);
            }
        }
        else { // CHAT
            const float winW = static_cast<float>(window.getSize().x);
            const float winH = static_cast<float>(window.getSize().y);
            const float chatX = SIDEBAR_W;
            const float chatW = winW - SIDEBAR_W;
 
            // --- Сайдбар со списком пользователей ---
            sf::RectangleShape sidebar({ SIDEBAR_W, winH });
            sidebar.setFillColor(Theme::sidebarBg);
            window.draw(sidebar);
 
            sf::Text brand(L"FEM CLUB", font, 20);
            brand.setStyle(sf::Text::Bold);
            brand.setFillColor(Theme::accent);
            brand.setPosition(14, 12);
            window.draw(brand);
 
            sf::Text onlineTitle(U("В сети — ") + std::to_wstring(onlineUsers.size()), font, 14);
            onlineTitle.setFillColor(Theme::textDim);
            onlineTitle.setPosition(14, 46);
            window.draw(onlineTitle);
 
            for (size_t i = 0; i < onlineUsers.size(); ++i) {
                sf::FloatRect row(6, 74.f + i * 38.f, SIDEBAR_W - 12, 34.f);
                bool sel = (onlineUsers[i] == selectedUser);
                drawRoundedRect(window, row.left, row.top, row.width, row.height, 8.f,
                    sel ? sf::Color(50, 45, 65) : Theme::sidebarBg);
 
                sf::CircleShape dot(4.f);
                dot.setFillColor(colorForName(onlineUsers[i]));
                dot.setPosition(row.left + 10, row.top + row.height / 2.f - 4.f);
                window.draw(dot);
 
                sf::Text t(onlineUsers[i], font, 15);
                t.setPosition(row.left + 26, row.top + 7);
                t.setFillColor(sel ? sf::Color::White : Theme::textMain);
                window.draw(t);
            }
 
            // --- Лента сообщений в виде пузырей ---
            const float bubblePad = 12.f;
            const float bubbleMaxWidth = std::min(460.f, chatW * 0.6f);
            float y = winH - TOOLBAR_H - INPUT_H - 10.f + chatScroll;
 
            fileClickRects.clear();
 
            for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
                ChatMessage& m = *it;
                bool mine = (m.kind != MsgKind::SYSTEM && m.author == myNick);
 
                if (m.kind == MsgKind::SYSTEM) {
                    auto lines = wrapText(m.text, font, 13, chatW - 80.f);
                    float h = lines.size() * 18.f + 10.f;
                    y -= h + 8.f;
                    if (y > winH - TOOLBAR_H - INPUT_H || y + h < 30.f) continue;
 
                    float maxLineW = 0.f;
                    sf::Text probe(L"", font, 13);
                    for (auto& l : lines) { probe.setString(l); maxLineW = std::max(maxLineW, probe.getLocalBounds().width); }
                    float w = maxLineW + 24.f;
                    float x = chatX + (chatW - w) / 2.f;
 
                    drawRoundedRect(window, x, y, w, h, h / 2.f, Theme::bubbleSystem);
                    for (size_t li = 0; li < lines.size(); ++li) {
                        sf::Text t(lines[li], font, 13);
                        t.setFillColor(Theme::textDim);
                        sf::FloatRect tb = t.getLocalBounds();
                        t.setPosition(x + (w - tb.width) / 2.f - tb.left, y + 5.f + li * 18.f);
                        window.draw(t);
                    }
                    continue;
                }
 
                auto lines = wrapText(m.text, font, 15, bubbleMaxWidth - bubblePad * 2.f);
                float maxLineW = 0.f;
                sf::Text probe(L"", font, 15);
                for (auto& l : lines) { probe.setString(l); maxLineW = std::max(maxLineW, probe.getLocalBounds().width); }
 
                float bubbleW = std::min(bubbleMaxWidth, maxLineW + bubblePad * 2.f);
                bubbleW = std::max(bubbleW, 50.f);
                bool showName = !mine;
                float nameH = showName ? 18.f : 0.f;
                float bubbleH = lines.size() * 19.f + bubblePad * 1.6f + nameH;
 
                y -= bubbleH + 8.f;
                if (y > winH - TOOLBAR_H - INPUT_H) continue;
                if (y + bubbleH < 30.f) continue;
 
                float bx = mine ? (chatX + chatW - 16.f - bubbleW) : (chatX + 16.f);
 
                sf::Color bubbleColor = mine ? Theme::bubbleMine : Theme::bubbleOther;
                drawRoundedRect(window, bx, y, bubbleW, bubbleH, 14.f, bubbleColor);
 
                float textY = y + bubblePad * 0.7f;
                if (showName) {
                    sf::Text nameT(m.author, font, 13);
                    nameT.setStyle(sf::Text::Bold);
                    nameT.setFillColor(colorForName(m.author));
                    nameT.setPosition(bx + bubblePad, textY);
                    window.draw(nameT);
                    textY += nameH;
                }
 
                sf::Color textColor = (m.kind == MsgKind::IMAGE || m.kind == MsgKind::VIDEO)
                    ? sf::Color(255, 240, 250) : (mine ? sf::Color::White : Theme::textMain);
 
                for (size_t li = 0; li < lines.size(); ++li) {
                    sf::Text t(lines[li], font, 15);
                    t.setFillColor(textColor);
                    t.setPosition(bx + bubblePad, textY + li * 19.f);
                    window.draw(t);
                }
 
                if (!m.filePath.empty()) {
                    fileClickRects.push_back({ sf::FloatRect(bx, y, bubbleW, bubbleH), m.filePath });
                }
            }
 
            // --- Панель инструментов ---
            sf::RectangleShape toolbar({ chatW, TOOLBAR_H });
            toolbar.setPosition(chatX, winH - TOOLBAR_H - INPUT_H);
            toolbar.setFillColor(Theme::sidebarBg);
            window.draw(toolbar);
 
            photoButton.rect = { chatX + 12, winH - TOOLBAR_H - INPUT_H + 6, 100, 38 };
            videoButton.rect = { chatX + 122, winH - TOOLBAR_H - INPUT_H + 6, 100, 38 };
            callButton.rect = { chatX + 232, winH - TOOLBAR_H - INPUT_H + 6, 190, 38 };
            photoButton.hovered = photoButton.contains(mouse);
            videoButton.hovered = videoButton.contains(mouse);
            callButton.hovered = callButton.contains(mouse);
            photoButton.draw(window, font, sf::Color(55, 52, 68), sf::Color(70, 66, 86));
            videoButton.draw(window, font, sf::Color(55, 52, 68), sf::Color(70, 66, 86));
 
            std::wstring callLabel = selectedUser.empty() ? U("Выбери собеседника") : (U("Позвонить: ") + selectedUser);
            callButton.label = callLabel;
            callButton.enabled = (callState == CallState::NONE && !selectedUser.empty());
            callButton.draw(window, font, sf::Color(60, 170, 110), sf::Color(75, 195, 130));
 
            // --- Поле ввода текста ---
            drawRoundedRect(window, chatX + 12, winH - INPUT_H + 8, chatW - 24, INPUT_H - 16, 12.f, Theme::inputBg);
 
            sf::Text inputText(messageInput.empty() ? U("Напишите сообщение и нажмите Enter...") : messageInput, font, 16);
            inputText.setFillColor(messageInput.empty() ? Theme::textDim : Theme::textMain);
            inputText.setPosition(chatX + 26, winH - INPUT_H + 18);
            window.draw(inputText);
 
            // --- Оверлей звонка ---
            if (callState == CallState::OUTGOING) {
                sf::RectangleShape dim({ winW, winH });
                dim.setFillColor(sf::Color(0, 0, 0, 120));
                window.draw(dim);
 
                drawRoundedRect(window, (winW - 360) / 2, (winH - 140) / 2, 360, 140, 16.f, Theme::panelBg);
                sf::Text t(U("Звоним ") + callPeer + L"...", font, 18);
                t.setPosition((winW - 360) / 2 + 24, (winH - 140) / 2 + 55);
                t.setFillColor(sf::Color::White);
                window.draw(t);
            }
            else if (callState == CallState::INCOMING) {
                sf::RectangleShape dim({ winW, winH });
                dim.setFillColor(sf::Color(0, 0, 0, 120));
                window.draw(dim);
 
                float ox = (winW - 380) / 2, oy = (winH - 170) / 2;
                drawRoundedRect(window, ox, oy, 380, 170, 16.f, Theme::panelBg);
                sf::Text t(U("Входящий звонок: ") + callPeer, font, 18);
                t.setPosition(ox + 24, oy + 24);
                t.setFillColor(sf::Color::White);
                window.draw(t);
 
                acceptButton.rect = { ox + 24, oy + 96, 150, 46 };
                declineButton.rect = { ox + 206, oy + 96, 150, 46 };
                acceptButton.hovered = acceptButton.contains(mouse);
                declineButton.hovered = declineButton.contains(mouse);
                acceptButton.draw(window, font, sf::Color(60, 170, 110), sf::Color(75, 195, 130));
                declineButton.draw(window, font, sf::Color(200, 70, 90), sf::Color(225, 90, 110));
            }
            else if (callState == CallState::ACTIVE) {
                drawRoundedRect(window, chatX + 20, 14, 340, 52, 14.f, sf::Color(35, 90, 65, 235));
                sf::Text t(U("В разговоре с ") + callPeer, font, 16);
                t.setPosition(chatX + 40, 30);
                t.setFillColor(sf::Color::White);
                window.draw(t);
 
                hangupButton.rect = { chatX + 20 + 160, 20, 190, 40 };
                hangupButton.hovered = hangupButton.contains(mouse);
                hangupButton.draw(window, font, sf::Color(200, 70, 90), sf::Color(225, 90, 110));
            }
        }
 
        window.display();
    }
 
    if (netThread.joinable()) netThread.join();
    return 0;
}
