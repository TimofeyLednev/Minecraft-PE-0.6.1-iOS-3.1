#include "OptionsScreen.h"

#include "StartMenuScreen.h"
#include "DialogDefinitions.h"
#include "../../Minecraft.h"
#include "../../../AppPlatform.h"

#include "../components/OptionsPane.h"
#include "../components/ImageButton.h"
#include "../components/OptionsGroup.h"
OptionsScreen::OptionsScreen()
: btnClose(NULL),
  bHeader(NULL),
  btnUsername(NULL),
  selectedCategory(0),
  waitingForUsername(false) {
}

OptionsScreen::~OptionsScreen() {
	if(btnClose != NULL) {
		delete btnClose;
		btnClose = NULL;
	}
	if(bHeader != NULL) {
		delete bHeader,
		bHeader = NULL;
	}
	if(btnUsername != NULL) {
		delete btnUsername;
		btnUsername = NULL;
	}
	for(std::vector<Touch::TButton*>::iterator it = categoryButtons.begin(); it != categoryButtons.end(); ++it) {
		if(*it != NULL) {
			delete *it;
			*it = NULL;
		}
	}
	for(std::vector<OptionsPane*>::iterator it = optionPanes.begin(); it != optionPanes.end(); ++it) {
		if(*it != NULL) {
			delete *it;
			*it = NULL;
		}
	}
	categoryButtons.clear();
}

void OptionsScreen::init() {
	bHeader = new Touch::THeader(0, "Options");
	btnClose = new ImageButton(1, "");
	ImageDef def;
	def.name = "gui/touchgui.png";
	def.width = 34;
	def.height = 26;

	def.setSrc(IntRectangle(150, 0, (int)def.width, (int)def.height));
	btnClose->setImageDef(def, true);

	categoryButtons.push_back(new Touch::TButton(2, "Account"));
	categoryButtons.push_back(new Touch::TButton(3, "Game"));
	categoryButtons.push_back(new Touch::TButton(4, "Controls"));
	categoryButtons.push_back(new Touch::TButton(5, "Graphics"));
	buttons.push_back(bHeader);
	buttons.push_back(btnClose);
	for(std::vector<Touch::TButton*>::iterator it = categoryButtons.begin(); it != categoryButtons.end(); ++it) {
		buttons.push_back(*it);
		tabButtons.push_back(*it);
	}
	generateOptionScreens();

}
void OptionsScreen::setupPositions() {
	int buttonHeight = btnClose->height;
	btnClose->x = width - btnClose->width;
	btnClose->y = 0;
	int offsetNum = 1;
	for(std::vector<Touch::TButton*>::iterator it = categoryButtons.begin(); it != categoryButtons.end(); ++it) {
		(*it)->x = 0;
		(*it)->y = offsetNum * buttonHeight;
		(*it)->selected = false;
		offsetNum++;
	}
	bHeader->x = 0;
	bHeader->y = 0;
	bHeader->width = width - btnClose->width;
	bHeader->height = btnClose->height;
	for(std::vector<OptionsPane*>::iterator it = optionPanes.begin(); it != optionPanes.end(); ++it) {
		if(categoryButtons.size() > 0 && categoryButtons[0] != NULL) {
			(*it)->x = categoryButtons[0]->width;
			(*it)->y = bHeader->height;
			(*it)->width = width - categoryButtons[0]->width;
			(*it)->setupPositions();
		}
	}
	if(btnUsername != NULL && categoryButtons.size() > 0) {
		btnUsername->x = categoryButtons[0]->width + 10;
		btnUsername->y = bHeader->height + 15;
		btnUsername->width = width - categoryButtons[0]->width - 20;
	}
	selectCategory(0);
}

void OptionsScreen::render( int xm, int ym, float a ) {
	renderBackground();
	super::render(xm, ym, a);
	int xmm = xm * width / minecraft->width;
	int ymm = ym * height / minecraft->height - 1;
	if(currentOptionPane != NULL)
		currentOptionPane->render(minecraft, xmm, ymm);
}

void OptionsScreen::removed()
{
}
void OptionsScreen::buttonClicked( Button* button ) {
	if(button == btnClose) {
		minecraft->reloadOptions();
		minecraft->screenChooser.setScreen(SCREEN_STARTMENU);
	} else if(button == btnUsername && btnUsername->visible) {
		minecraft->platform()->createUserInput(DialogDefinitions::DIALOG_SET_USERNAME);
		waitingForUsername = true;
	} else if(button->id > 1 && button->id < 7) {
		// This is a category button
		int categoryButton = button->id - categoryButtons[0]->id;
		selectCategory(categoryButton);
	}
}

void OptionsScreen::selectCategory( int index ) {
	int currentIndex = 0;
	for(std::vector<Touch::TButton*>::iterator it = categoryButtons.begin(); it != categoryButtons.end(); ++it) {
		if(index == currentIndex) {
			(*it)->selected = true;
		} else {
			(*it)->selected = false;
		}
		currentIndex++;
	}
	if(index < (int)optionPanes.size())
		currentOptionPane = optionPanes[index];
	// Show username button only on Account tab
	if(btnUsername != NULL)
		btnUsername->visible = (index == 0);
}

void OptionsScreen::generateOptionScreens() {
	optionPanes.push_back(new OptionsPane());
	optionPanes.push_back(new OptionsPane());
	optionPanes.push_back(new OptionsPane());
	optionPanes.push_back(new OptionsPane());

	// Account Pane
	btnUsername = new Touch::TButton(10, minecraft->options.username);
	buttons.push_back(btnUsername);
	optionPanes[0]->createOptionsGroup("options.group.mojang");

	// Game Pane
	optionPanes[1]->createOptionsGroup("options.group.game")
		.addOptionItem(&Options::Option::THIRD_PERSON, minecraft)
		.addOptionItem(&Options::Option::SERVER_VISIBLE, minecraft)
		.addOptionItem(&Options::Option::DIFFICULTY, minecraft);

	// Controls Pane
	optionPanes[2]->createOptionsGroup("options.group.controls")
		.addOptionItem(&Options::Option::SENSITIVITY, minecraft)
		.addOptionItem(&Options::Option::INVERT_MOUSE, minecraft)
		.addOptionItem(&Options::Option::LEFT_HANDED, minecraft)
		.addOptionItem(&Options::Option::USE_TOUCHSCREEN, minecraft)
		.addOptionItem(&Options::Option::USE_TOUCH_JOYPAD, minecraft);
	optionPanes[2]->createOptionsGroup("options.group.feedback")
		.addOptionItem(&Options::Option::DESTROY_VIBRATION, minecraft);

	// Graphics Pane
	optionPanes[3]->createOptionsGroup("options.group.graphics")
		.addOptionItem(&Options::Option::PIXELS_PER_MILLIMETER, minecraft)
		.addOptionItem(&Options::Option::GRAPHICS, minecraft)
		.addOptionItem(&Options::Option::VIEW_BOBBING, minecraft)
		.addOptionItem(&Options::Option::AMBIENT_OCCLUSION, minecraft);
}

void OptionsScreen::mouseClicked( int x, int y, int buttonNum ) {
	if(currentOptionPane != NULL)
		currentOptionPane->mouseClicked(minecraft, x, y, buttonNum);
	super::mouseClicked(x, y, buttonNum);
}

void OptionsScreen::mouseReleased( int x, int y, int buttonNum ) {
	if(currentOptionPane != NULL)
		currentOptionPane->mouseReleased(minecraft, x, y, buttonNum);
	super::mouseReleased(x, y, buttonNum);
}

void OptionsScreen::tick() {
	if(waitingForUsername) {
		int status = minecraft->platform()->getUserInputStatus();
		if(status > -1) {
			if(status == 1) {
				StringVector sv = minecraft->platform()->getUserInput();
				if(!sv.empty() && sv[0].length() > 0) {
					minecraft->options.username = sv[0];
					btnUsername->msg = sv[0];
					minecraft->options.save();
				}
			}
			waitingForUsername = false;
		}
	}
	if(currentOptionPane != NULL)
		currentOptionPane->tick(minecraft);
	super::tick();
}
