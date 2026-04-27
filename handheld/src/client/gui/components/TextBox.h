#ifndef NET_MINECRAFT_CLIENT_GUI_COMPONENTS__TextBox_H__
#define NET_MINECRAFT_CLIENT_GUI_COMPONENTS__TextBox_H__

//package net.minecraft.client.gui;

#include <string>
#include "GuiElement.h"
#include "../../Options.h"
#include "../../../platform/input/Mouse.h"
#include "../../../platform/input/Keyboard.h"

class Font;
class Minecraft;

class TextBox : public GuiElement
{
public:
	TextBox(int id, const std::string& msg);
	TextBox(int id, int x, int y, const std::string& msg);
	TextBox(int id, int x, int y, int w, int h, const std::string& msg);

	virtual void mouseClicked(Minecraft* minecraft, int x, int y, int buttonNum);

	virtual void setFocus(Minecraft* minecraft);
	virtual bool loseFocus(Minecraft* minecraft);

	virtual void render(Minecraft* minecraft, int xm, int ym);

	virtual void handleKey(int key);
	virtual void handleChar(char c);
	virtual void tick(Minecraft* minecraft);

public:
	std::string hint;
	std::string text;
	int id;

	int blinkTicks;

	bool focused;
	bool blink;
};

#endif /*NET_MINECRAFT_CLIENT_GUI_COMPONENTS__TextBox_H__*/
