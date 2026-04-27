#include "JoinByIPScreen.h"

#include "JoinGameScreen.h"
#include "StartMenuScreen.h"
#include "ProgressScreen.h"
#include "../Font.h"
#include "../../../network/RakNetInstance.h"
#include "../../../client/Options.h"
#include "../../../client/gui/Screen.h"
#include "../../../client/gui/components/TextBox.h"
#include "../../../network/ClientSideNetworkHandler.h"

JoinByIPScreen::JoinByIPScreen() :
	tIP(0, "Server IP"),
	bHeader(1, "Join on server"),
	bJoin(2, "Join Game"),
	bBack(3, "")
{
	bJoin.active = false;
	//gamesList->yInertia = 0.5f;
}

JoinByIPScreen::~JoinByIPScreen()
{
}

void JoinByIPScreen::buttonClicked(Button* button)
{
	if (button->id == bJoin.id)
	{
		minecraft->isLookingForMultiplayer = true;
		minecraft->netCallback = new ClientSideNetworkHandler(minecraft, minecraft->raknetInstance);

		minecraft->joinMultiplayerFromString(tIP.text);
		{
			minecraft->options.lastIp = tIP.text;
			minecraft->options.save();
			bJoin.active = false;
			bBack.active = false;
			minecraft->setScreen(new ProgressScreen());
		}
	}
	if (button->id == bBack.id)
	{
		minecraft->cancelLocateMultiplayer();
		minecraft->screenChooser.setScreen(SCREEN_STARTMENU);
	}
}

bool JoinByIPScreen::handleBackEvent(bool isDown)
{
	if (!isDown)
	{
		minecraft->screenChooser.setScreen(SCREEN_STARTMENU);
	}
	return true;
}

void JoinByIPScreen::tick()
{
	Screen::tick();
	bJoin.active = !tIP.text.empty();
}

void JoinByIPScreen::init()
{
	ImageDef def;
	def.name = "gui/touchgui.png";
	def.width = 34;
	def.height = 26;

	def.setSrc(IntRectangle(150, 0, (int)def.width, (int)def.height));
	bBack.setImageDef(def, true);

	buttons.push_back(&bJoin);
	buttons.push_back(&bBack);
	buttons.push_back(&bHeader);

	textBoxes.push_back(&tIP);
#ifdef ANDROID
	tabButtons.push_back(&bJoin);
	tabButtons.push_back(&bBack);
	tabButtons.push_back(&bHeader);
#endif

	tIP.text = minecraft->options.lastIp;
}

void JoinByIPScreen::setupPositions() {
	int tIpDiff = 40;

	bJoin.y = height * 2 / 3;
	bBack.y = 0;
	bHeader.y = 0;

	// Center buttons
	//bJoin.x = width / 2 - 4 - bJoin.w;
	bBack.x = width - bBack.width;//width / 2 + 4;

	bJoin.x = (width - bJoin.width) / 2;

	bHeader.x = 0;
	bHeader.width = width - bBack.width;

	tIP.width = bJoin.width + tIpDiff;
	tIP.height = 16;
	tIP.x = bJoin.x - tIpDiff / 2;
	tIP.y = ((height - bJoin.height) / 2) - tIP.height - 4;
}

void JoinByIPScreen::render(int xm, int ym, float a)
{
	renderBackground();
	Screen::render(xm, ym, a);
}

void JoinByIPScreen::keyPressed(int eventKey)
{
	if (eventKey == Keyboard::KEY_ESCAPE) {
		minecraft->screenChooser.setScreen(SCREEN_STARTMENU);
		return;
	}
	// let base class handle navigation and text box keys
	Screen::keyPressed(eventKey);
}