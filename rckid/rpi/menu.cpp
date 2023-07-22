#include "utils/json.h"

#include "window.h"
#include "menu.h"

void Menu::clear() {
    for (Item * i : items_)
        delete i;
    items_.clear();
}

void Menu::onSelectItem(size_t index) { 
    items_[index]->onSelect(); 
}

void Menu::Item::initialize() {
    img_ = LoadTexture(imgFile_.c_str());
    Vector2 fs = MeasureText(window().menuFont(), title_.c_str(), MENU_FONT_SIZE);
    titleWidth_ = fs.x;
}

void WidgetItem::onSelect() {
    window().setWidget(widget_);
}

void Submenu::onSelect() {
    window().setMenu(submenu_);
}



/*
void JSONItem::onSelect() {
    try {
        json::Value v{json::parseFile(submenu_.jsonFile_)};
        for (json::Value const & v : v.arrayElements()) {
            std::string const & title = v["title"].value<std::string>();
            std::string const & image = v["image"].value<std::string>();
            submenu_.add(new Menu::Item(title, image));
        }
        window->setMenu(& submenu_);
    } catch (std::runtime_error const & e) {
        TraceLog(LOG_ERROR, e.what());
    }
}
*/