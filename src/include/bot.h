/// @file bot.h
/// @author EtoileScintillante
/// @brief The following file contains a simple bot class.
///        The bot is created with the D++/DPP library.
///        It supports slash commands and its purpose is to fetch equity data 
///        and visualize it. Data is fetched from Yahoo Finance and may be delayed.
/// @date 2023-08-09

#ifndef BOT_H
#define BOT_H

#include <dpp/dpp.h>
#include <chrono>
#include <thread>
#include <string>
#include <fstream>
#include <vector>
#include "data.h"
#include "visualize.h"

class Bot
{
public:
    Bot(const std::string &token);

    void run();

private:
    void setupBot();
    void commandHandler(const dpp::slashcommand_t &event);
    void onReady(const dpp::ready_t &event);
    void registerCommands();

    dpp::cluster bot;
};

#endif // BOT_H
