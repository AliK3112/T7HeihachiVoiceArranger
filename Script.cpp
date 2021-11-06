#include <sstream>
#include <limits>
#include <iostream>
#include <Windows.h>
#include <TlHelp32.h>
#include <tchar.h>
#include <thread>
#include <fstream>
#include <string.h>
#include <stdlib.h>
#include <list>

using namespace std;

// Structure for storing data from the textfiles
struct element
{
	string movename = "\0";
	int move_id = 0;
	int voiceclip_val = 0;
	int extraprop_val = 0;
	int extraprop_num = 0;
};

// Moveset Structure Offsets
enum MOVESET_STRUCTURE_OFFSETS {
	reaction_list = 0x150,
	requirements = 0x160,
	hit_condition = 0x170,
	projectile = 0x180,
	pushback = 0x190,
	pushback_extra = 0x1A0,
	cancel_list = 0x1B0,
	grp_cancel_list = 0x1C0,
	cancel_extra = 0x1D0,
	extraprops = 0x1E0,
	moves_list = 0x210,
	voice_clip = 0x220
};

// Move Attribute Offsets
enum MoveAttributeOffsets {
	name = 0x0,
	anim_name = 0x8,
	anim_addr = 0x10,
	vuln = 0x18,
	hitlevel = 0x1c,
	cancel_addr = 0x20,
	transition = 0x54,
	anim_len = 0x68,
	startup = 0xA0,
	recovery = 0xA4,
	hitbox = 0x9C,
	hit_cond_addr = 0x60,
	ext_prop_addr = 0x80,
	voiceclip_addr = 0x78
};

// Variable to Handle the process
HANDLE processHandle = NULL;

// Base Address of the Game
uintptr_t gameBaseAddress = 0;

// Addresses read from addresses.txt file
uintptr_t p1_struct = 0;
uintptr_t p1_struct_size = 0;
uintptr_t p1_moveset_offset = 0;

// List that stores all data about the voiceclips
list<element> obj;

// This variable stores if the game is running or not
bool gameRunning = true;

// This function checks if Tekken 7 is still running or not
bool isRunning(LPCWSTR pName);

// This function returns the Base Address of a given module based on Process ID and Name
uintptr_t GetModuleBaseAddress(TCHAR* lpszModuleName, uintptr_t pID);

// This function checks if the handle for the window belongs to TEKKEN 7.
bool isTekken7(const char* ptr);

// Main function of our script
void MainFunction();

// This function rearranges Heihachi's voiceclips
bool RearrangeVoiceclips(uintptr_t moveset);

// This function checks if a moveset is loaded or not
bool isMovesetLoaded(uintptr_t addr);

// This function checks if desired character is selected
int CheckCharacter(uintptr_t moveset);

// This function fetches the address of the moveset of given side
uintptr_t GetMovesetAddress(int side);

// This function finds and returns the address of a given move. Returns 0 on error or if move not found
uintptr_t GetMoveAddress(uintptr_t moveset, const char* moveName, int start_index = 0);

// This function changes voiceclip to given value
bool ChangeVoiceclip(uintptr_t move_addr, int voiceclip_val);

// This function changes voiceclip extraprop to given value
bool ChangeVoiceclipExtraprop(uintptr_t move_addr, int voiceclip_val, int num = 1);

// This Thread function checks if the game is running or not
void ThreadGameRunning();

int main()
{
	int c = 1;
	HWND hGameWindow = NULL;
	while (1)
	{
		hGameWindow = FindWindowEx(0, 0, TEXT("UnrealWindow"), 0);
		if (hGameWindow == NULL)
		{
			if (c == 1) {
				std::cout << "Game window not found.\nPlease Run TEKKEN 7\nWaiting...";
				c++;
			}
			Sleep(1000);
		}
		else break;
	}
	std::cout << "Window Found!\n";
	uintptr_t pID = 0;
	GetWindowThreadProcessId(hGameWindow, (LPDWORD)&pID);
	// Checking if the correct window is selected or not
	if (!isRunning(L"UnrealWindow"))
	{
		std::cout << "Wrong window attached.\nClosing program...";
		Sleep(2500);
		return 0;
	}

	processHandle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, (DWORD)pID);
	if (processHandle == INVALID_HANDLE_VALUE || processHandle == NULL)
	{
		std::cout << "Unable to attach to TEKKEN 7.\nClosing Program...";
		Sleep(2500);
		return 0;
	}

	std::cout << "Script Attached to Tekken 7\n";
	TCHAR proc_name[] = TEXT("TekkenGame-Win64-Shipping.exe");
	gameBaseAddress = GetModuleBaseAddress(proc_name, pID);
	std::thread t1(ThreadGameRunning);
	MainFunction();
	t1.join();
	obj.~list();
	return 0;
}

void MainFunction()
{
	// Reading Addresses from text file
	ifstream read;
	read.open("addresses.txt", ios::in);
	if(!read.is_open())
	{
		printf("Unable to open file: addresses.txt\nExiting Program\n");
		exit(1);
	}
	string input = "\0";
	string temp = "\0";
	uintptr_t* ptr[3] = {&p1_struct, &p1_struct_size, &p1_moveset_offset};
	int c = 0;
	while(getline(read, input))
	{
		try
		{
			input = input.substr(input.find("x") + 1);
		}
		catch(exception &e)
		{
			printf("Invalid data written in the file: addresses.txt\nExiting Program\n");
			exit(1);	
		}
		*(ptr[c++]) = strtoll(input.c_str(), 0, 16);
	}
	read.close();
	
	// Reading data about the voicelines
	element elm;
	read.open("voiceclips_data.txt", ios::in);
	if (!read.is_open())
	{
		cout << "Unable to open file!\n";
		exit(1);
	}
	input = "\0";
	while(getline(read, input))
	{
		if (input[0] == '#') continue;
		stringstream str(input);
		str >> elm.movename >> elm.move_id >> elm.voiceclip_val >> elm.extraprop_val >> elm.extraprop_num;
		if (str.fail())
		{
			printf("Invalid entry in the text file\n");
			str.clear();
			str.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
			continue;
		}
		obj.push_back(elm);
	}
	read.close();
	
	bool p1written = false;
	bool p2written = false;
	uintptr_t moveset1 = 0;
	uintptr_t moveset2 = 0;
	// Main Loop
	while (gameRunning)
	{
		Sleep(10);	// Reducing CPU usage by sleeping
		moveset1 = GetMovesetAddress(0);
		moveset2 = GetMovesetAddress(1);
		if (!isMovesetLoaded(moveset1)) {
			p1written = false;
			p2written = false;
			continue;
		}
		
		// For Player 1
		if (!p1written && (CheckCharacter(moveset1) == 8)) p1written = RearrangeVoiceclips(moveset1);

		// For Player 2
		if (!p2written && (CheckCharacter(moveset2) == 8)) p2written = RearrangeVoiceclips(moveset2);
	}
}

bool RearrangeVoiceclips(uintptr_t moveset)
{
	system("cls");
	auto starting_time = std::chrono::high_resolution_clock::now();
	int result = 1;
	/////////////////////////////////////////////////////////////////////////////
	// This is the portion to re-arrange voice clips
	uintptr_t move_addr = 0;
	list<element>::iterator it;
    for(it = obj.begin(); it != obj.end(); ++it)
    {
    	move_addr = GetMoveAddress(moveset, (*it).movename.c_str(), (*it).move_id - 150);
    	if (move_addr == 0) {
			printf("move: %s does not exist\n", (*it).movename.c_str());
			continue;
		}
		printf("Changing voiceclip of move: %s\n", (*it).movename.c_str());
		
		// Changing voiceclip
		if ((*it).voiceclip_val != 0)
		{
			ChangeVoiceclip(move_addr, (*it).voiceclip_val);
		}
		
		if ((*it).extraprop_val != -1)
		{
			ChangeVoiceclipExtraprop(move_addr, (*it).extraprop_val, (*it).extraprop_num);
		}
	}
	
	/////////////////////////////////////////////////////////////////////////////
	auto stopping_time = std::chrono::high_resolution_clock::now();
	auto time_taken = std::chrono::duration_cast<std::chrono::milliseconds>(stopping_time - starting_time);
	if (result == 1) {
		std::cout << "Time taken to re-arrange voice clips: " << time_taken.count() << " milliseconds\n";
	}
	if (result >= 0)
		return true;
	return false;
}

bool isMovesetLoaded(uintptr_t moveset)
{
	if (moveset == 0) return false;
	int value = 0;
	ReadProcessMemory(processHandle, (LPVOID)(moveset), &value, sizeof(int), NULL);
	if (value != 0x10000) return false;
	return true;
}

int CheckCharacter(uintptr_t moveset)
{
	short id = -1;
	ReadProcessMemory(processHandle, (LPVOID)(moveset + 0x14E), &id, sizeof(id), NULL);
	return id;
}

uintptr_t GetMovesetAddress(int side)
{
	if (side < 0 || side > 1) return 0;
	uintptr_t moveset = 0;
	ReadProcessMemory(processHandle, (LPVOID)(gameBaseAddress + p1_struct + p1_moveset_offset + ((uintptr_t)side * p1_struct_size)), &moveset, sizeof(uintptr_t), NULL);
	return moveset;
}

uintptr_t GetMoveAddress(uintptr_t moveset, const char* moveName, int start_index)
{
	uintptr_t moves_addr = 0;
	unsigned int moves_size = 0;
	ReadProcessMemory(processHandle, (LPVOID)(moveset + 0x210), &moves_addr, sizeof(moves_addr), NULL);
	ReadProcessMemory(processHandle, (LPVOID)(moveset + 0x218), &moves_size, sizeof(moves_size), NULL);
	if (moves_addr == 0) return 0;
	uintptr_t addr = 0, moveNameAddr = 0;
	char name[30]{ 0 };
	for (unsigned int i = 0; i < 30; i++) name[i] = 0;
	if (start_index < 0) start_index = 0;
	for (unsigned int i = start_index; i < moves_size; i++)
	{
		addr = moves_addr + (uintptr_t)(i * 176);
		ReadProcessMemory(processHandle, (LPVOID)(addr), &moveNameAddr, sizeof(moveNameAddr), NULL);
		ReadProcessMemory(processHandle, (LPVOID)(moveNameAddr), &name, sizeof(name), NULL);
		if (strcmp(name, moveName) == 0) return addr;
	}
	return 0;
}

bool ChangeVoiceclip(uintptr_t move_addr, int voiceclip_val)
{
	if (move_addr == 0) return false;
	uintptr_t voiceclip_addr = 0;
	int val = 0;
	ReadProcessMemory(processHandle, (LPVOID)(move_addr + MoveAttributeOffsets::voiceclip_addr), &voiceclip_addr, sizeof(uintptr_t), NULL);
	if (voiceclip_addr == 0) return true;
	ReadProcessMemory(processHandle, (LPVOID)(voiceclip_addr + 8), &val, sizeof(val), NULL);
	if (val == -1) return true;
	WriteProcessMemory(processHandle, (LPVOID)(voiceclip_addr + 8), &voiceclip_val, sizeof(voiceclip_val), NULL);
	return true;
}

bool ChangeVoiceclipExtraprop(uintptr_t move_addr, int voiceclip_val, int num)
{
	if (move_addr == 0) return false;
	uintptr_t extraprop_addr = 0;
	int starting_frame = -1, type = -1, val = -1;
	ReadProcessMemory(processHandle, (LPVOID)(move_addr + MoveAttributeOffsets::ext_prop_addr), &extraprop_addr, sizeof(uintptr_t), NULL);
	if (extraprop_addr == 0) return true;
	uintptr_t addr = extraprop_addr;
	if (num < 1) num = 1;
	int count = num;
	while(1)
	{
		ReadProcessMemory(processHandle, (LPVOID)(addr + 0), &starting_frame, sizeof(starting_frame), NULL);
		ReadProcessMemory(processHandle, (LPVOID)(addr + 4), &type, sizeof(type), NULL);
		ReadProcessMemory(processHandle, (LPVOID)(addr + 8), &val, sizeof(val), NULL);
		if ((starting_frame == 0) && (type == 0) && (val == 0)) {
			break;
		}
		if (type == 0x84c4) {
			count--;
		}
		if (count == 0) {
			WriteProcessMemory(processHandle, (LPVOID)(addr + 8), &voiceclip_val, sizeof(voiceclip_val), NULL);
			break;
		}
		addr += 12;
	}
	return true;
}

uintptr_t GetModuleBaseAddress(TCHAR* lpszModuleName, uintptr_t pID)
{
	uintptr_t dwModuleBaseAddress = 0;
	HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, (DWORD)pID); // make snapshot of all modules within process
	MODULEENTRY32 ModuleEntry32 = { 0 };
	ModuleEntry32.dwSize = sizeof(MODULEENTRY32);
	if (Module32First(hSnapshot, &ModuleEntry32)) //store first Module in ModuleEntry32
	{
		do {
			if (_tcscmp(ModuleEntry32.szModule, lpszModuleName) == 0) // if Found Module matches Module we look for -> done!
			{
				dwModuleBaseAddress = (uintptr_t)ModuleEntry32.modBaseAddr;
				break;
			}
		} while (Module32Next(hSnapshot, &ModuleEntry32)); // go through Module entries in Snapshot and store in ModuleEntry32
	}
	CloseHandle(hSnapshot);
	return dwModuleBaseAddress;
}

bool isRunning(LPCWSTR pName)
{
	HWND hwnd = FindWindowEx(0, 0, TEXT("UnrealWindow"), 0);
	if (hwnd)
	{
		char title[20] = { 0 };
		GetWindowText(hwnd, (LPSTR)title, sizeof(title));
		if (isTekken7(title)) return true;
		return false;
	}
	else return false;
}

bool isTekken7(const char* ptr)
{
	if (strcmp("TEKKEN 7 ", ptr) == 0)
		return true;
	return false;
}

void ThreadGameRunning()
{
	HWND hwnd;
	char title[20] = { 0 };
	while (1)
	{
		hwnd = FindWindowEx(0, 0, TEXT("UnrealWindow"), 0);
		if (hwnd)
		{
			GetWindowText(hwnd, (LPSTR)title, sizeof(title));
			if (strcmp("TEKKEN 7 ", title))	gameRunning = false;
			if (!gameRunning) break;
			gameRunning = true;
		}
		else { gameRunning = false; break; }
		Sleep(10);
	}
	std::cout << "Game No longer running. Closing program\n";
	Sleep(2500);
	exit(0);
}
