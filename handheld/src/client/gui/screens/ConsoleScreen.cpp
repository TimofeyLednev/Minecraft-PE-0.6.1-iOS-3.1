#include "ConsoleScreen.h"
#include "../Gui.h"
#include "../../Minecraft.h"
#include "../../player/LocalPlayer.h"
#include "../../../platform/input/Keyboard.h"
#include "../../../world/level/Level.h"
#include "../../../network/RakNetInstance.h"
#include "../../../network/ServerSideNetworkHandler.h"
#include "../../../network/packet/ChatPacket.h"
#include "../../../platform/log.h"

#include <sstream>
#include <cstdlib>
#include <cctype>

ConsoleScreen::ConsoleScreen()
:   _input(""),
    _cursorBlink(0)
{
}

void ConsoleScreen::init()
{
    minecraft->platform()->showKeyboard();
}

void ConsoleScreen::tick()
{
    _cursorBlink++;
}

bool ConsoleScreen::handleBackEvent(bool /*isDown*/)
{
    minecraft->platform()->hideKeyboard();
    minecraft->setScreen(NULL);
    return true;
}

void ConsoleScreen::keyPressed(int eventKey)
{
    if (eventKey == Keyboard::KEY_ESCAPE) {
        minecraft->platform()->hideKeyboard();
        minecraft->setScreen(NULL);
    } else if (eventKey == Keyboard::KEY_RETURN) {
        execute();
    } else if (eventKey == Keyboard::KEY_BACKSPACE) {
        if (!_input.empty())
            _input.erase(_input.size() - 1, 1);
    } else {
        super::keyPressed(eventKey);
    }
}

void ConsoleScreen::keyboardNewChar(char inputChar)
{
    if (inputChar >= 32 && inputChar < 127)
        _input += inputChar;
}

// ---------------------------------------------------------------------------
// execute
// ---------------------------------------------------------------------------
void ConsoleScreen::execute()
{
    minecraft->platform()->hideKeyboard();

    if (_input.empty()) {
        minecraft->setScreen(NULL);
        return;
    }

    if (_input[0] == '/') {
        if (minecraft->netCallback && !minecraft->raknetInstance->isServer()) {
            ChatPacket pkt(_input);
            minecraft->raknetInstance->send(pkt);
        } else {
            std::string result = processCommand(_input);
            if (!result.empty())
                minecraft->gui.addMessage(result);
        }
    } else {
        std::string msg = std::string("<") + minecraft->player->name + "> " + _input;
        if (minecraft->netCallback && minecraft->raknetInstance->isServer()) {
            static_cast<ServerSideNetworkHandler*>(minecraft->netCallback)->displayGameMessage(msg);
        } else if (minecraft->netCallback) {
            ChatPacket pkt(msg);
            minecraft->raknetInstance->send(pkt);
        } else {
            minecraft->gui.addMessage(msg);
        }
    }

    minecraft->setScreen(NULL);
}

// ---------------------------------------------------------------------------
// processCommand
// ---------------------------------------------------------------------------
static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
}

std::string ConsoleScreen::processCommand(const std::string& raw)
{
    std::string line = raw;
    if (!line.empty() && line[0] == '/') line = line.substr(1);
    line = trim(line);

    std::vector<std::string> args;
    {
        std::istringstream ss(line);
        std::string tok;
        while (ss >> tok) args.push_back(tok);
    }

    if (args.empty()) return "";

    Level* level = minecraft->level;
    if (!level) return "No level loaded.";

    if (args[0] == "time") {
        if (args.size() < 2)
            return "Usage: /time <add|set|query> ...";

        const std::string& sub = args[1];

        if (sub == "add") {
            if (args.size() < 3) return "Usage: /time add <value>";
            long delta = std::atol(args[2].c_str());
            long newTime = level->getTime() + delta;
            level->setTime(newTime);
            std::ostringstream out;
            out << "Set the time to " << (newTime % Level::TICKS_PER_DAY);
            return out.str();
        }

        if (sub == "set") {
            if (args.size() < 3) return "Usage: /time set <value|day|night|noon|midnight>";
            const std::string& val = args[2];

            long t = -1;
            if      (val == "day")      t = 1000;
            else if (val == "noon")     t = 6000;
            else if (val == "night")    t = 13000;
            else if (val == "midnight") t = 18000;
            else {
                bool numeric = true;
                for (size_t i = 0; i < val.size(); i++) {
                    if (!std::isdigit((unsigned char)val[i])) { numeric = false; break; }
                }
                if (!numeric) return std::string("Unknown value: ") + val;
                t = std::atol(val.c_str());
            }

            long dayCount = level->getTime() / Level::TICKS_PER_DAY;
            long newTime  = dayCount * Level::TICKS_PER_DAY + (t % Level::TICKS_PER_DAY);
            level->setTime(newTime);
            std::ostringstream out;
            out << "Set the time to " << t;
            return out.str();
        }

        if (sub == "query") {
            if (args.size() < 3) return "Usage: /time query <daytime|gametime|day>";
            const std::string& what = args[2];

            long total   = level->getTime();
            long daytime = total % Level::TICKS_PER_DAY;
            long day     = total / Level::TICKS_PER_DAY;

            std::ostringstream out;
            if      (what == "daytime")  { out << "The time of day is " << daytime; }
            else if (what == "gametime") { out << "The game time is "   << total;   }
            else if (what == "day")      { out << "The day is "         << day;     }
            else return std::string("Unknown query: ") + what;
            return out.str();
        }

        return "Unknown sub-command. Usage: /time <add|set|query> ...";
    }

    return std::string("Unknown command: /") + args[0];
}

// ---------------------------------------------------------------------------
// render
// ---------------------------------------------------------------------------
void ConsoleScreen::render(int /*xm*/, int /*ym*/, float /*a*/)
{
    fill(0, 0, width, height, 0x80000000);

    minecraft->gui.renderChatMessages(height, 30, true, font);

    const int boxH  = 12;
    const int boxY  = height - boxH - 2;
    const int boxX0 = 2;
    const int boxX1 = width - 2;

    fill(boxX0, boxY, boxX1, boxY + boxH, 0xc0000000);

    fill(boxX0,     boxY,            boxX1,     boxY + 1,        0xff808080);
    fill(boxX0,     boxY + boxH - 1, boxX1,     boxY + boxH,     0xff808080);
    fill(boxX0,     boxY,            boxX0 + 1, boxY + boxH,     0xff808080);
    fill(boxX1 - 1, boxY,            boxX1,     boxY + boxH,     0xff808080);

    std::string displayed = _input;
    if ((_cursorBlink / 10) % 2 == 0)
        displayed += '_';

    font->drawShadow(displayed, (float)(boxX0 + 2), (float)(boxY + 2), 0xffffffff);
}