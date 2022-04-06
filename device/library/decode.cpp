/* This file decodes commands sent by the server */

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <bits/stdc++.h>

#include "common.h"
using namespace std;

void print_incorrect_command() {
    cout << "error: the command is not correctly formatted.\n";
}

// https://www.geeksforgeeks.org/remove-a-given-word-from-a-string/
string removeWord(string str, string word) 
{
    if (str.find(word) != string::npos) {
        size_t p = -1;
  
        string tempWord = word + " ";
        while ((p = str.find(word)) != string::npos)
            str.replace(p, tempWord.length(), "");
  
        tempWord = " " + word;
        while ((p = str.find(word)) != string::npos)
            str.replace(p, tempWord.length(), "");
    }
    return str;
}

void decode_command(string command) {

    /* check for the case where the server needs all the logs */
    if (command == "ALL") {
        cout << "ALL: send back all data received within the last epoch.\n";
        dump_log();
        commit();
        return;
    } 

    /* remove the first word "FILTER" (if exists) */
    command = removeWord(command, "FILTER");

    /* split the command by space */
    istringstream command_split(command);
    string word;

    /* currently, the program only accepts a single filter criteria. 
       Do we want to have '&' and '|' operations? (TODO) */
    string rule = "";
    int index = 0;
    while (command_split >> word) {
        if (index == 0) {
            if (word != "KEY" && word != "SYSCALL" && word != "PNAME")
                print_incorrect_command();
        }
        if (index == 1) {
            rule += word;
        }
        index++;
    }

    cout << "Key is: " << rule << endl;

    filter_and_dump_log(rule);
    commit();
}

void handle_command(void) {
    /* open the command file for reading. */
    ifstream command_file (comm_filename);
    if (!command_file.is_open()) {
      cout << "error: couldn't open command file (" << comm_filename <<  ")." << endl;
      return;
    }

    /* commands are always sent one at a time. */
    string line;
    getline (command_file, line);
    cout << line << '\n';
    decode_command(line);
    command_file.close();
}
