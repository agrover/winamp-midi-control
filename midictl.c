// Winamp general purpose plug-in mini-SDK
// Copyright (C) 1997, Justin Frankel/Nullsoft

// MIDI Control plugin for WinAmp
// Copyright (C) 1999,2004,2005 Andy Grover

#include <windows.h>
#include <process.h>
#include <stdio.h>

#include "gen.h"
#include "resource.h"
#include "frontend.h"
#include "winamp.h"

#define NOTE_PRESSED	0x90

#define CONTROL 		0xb0
#define MOD_WHEEL		0x01

#define PITCH			0xe0
#define PITCH_WHEEL 	0x00

#define TASCAM_ON		0x7f
#define TASCAM_CONTROL	0xbf
#define TASCAM_REW		0x13
#define TASCAM_FFW		0x14
#define TASCAM_STOP 	0x15
#define TASCAM_PLAY 	0x16
#define TASCAM_REC		0x17
#define TASCAM_LOCATE_L 0x18
#define TASCAM_LOCATE_R 0x19
#define TASCAM_FADER	0x4b
#define TASCAM_DATAWHL	0x60

BOOL WINAPI _DllMainCRTStartup(HANDLE hInst, ULONG ul_reason_for_call, LPVOID lpReserved)
{
	return TRUE;
}

BOOL CALLBACK ConfigProc(HWND hwndDlg,UINT uMsg,WPARAM wParam,LPARAM lParam);
BOOL CALLBACK AutodetectProc(HWND hwndDlg,UINT uMsg,WPARAM wParam,LPARAM lParam);
typedef void (CALLBACK *midi_callback)(HMIDIIN hMidiIn,  UINT wMsg, DWORD dwInstance,
								 DWORD dwParam1, DWORD dwParam2);
void config();
void quit();
int init();
HMIDIIN midi_open(int device_num, midi_callback callback, ULONG userdata);
BOOL midi_close(HMIDIIN handle);
void config_write();
void config_read();

void CALLBACK midi_data_received(HMIDIIN hMidiIn,  UINT wMsg, DWORD dwInstance,
								 DWORD dwParam1, DWORD dwParam2);
void CALLBACK midi_data_autoconfig(HMIDIIN hMidiIn,  UINT wMsg, DWORD dwInstance,
								 DWORD dwParam1, DWORD dwParam2);



// Global Variables
char*		szAppName = "Midi Control";
char*		szErrorOpen = "Error Opening MIDI Device";
char*		szErrorClose = "Error Closing MIDI Device";
HMIDIIN 	midi_handle;
int 		midi_device_num = -1;
int 		midi_device_channel = 0;
int 		die = 0;

HANDLE hEvent;
ULONG ThreadId;
ULONG MidiData;
HMIDIIN* MidiDevArray;

HWND MainDlgWindow;

HWND AutoWindow;
int AutoChannel;
int AutoDevice;

winampGeneralPurposePlugin plugin =
{
	GPPHDR_VER,
	"", 			// filled in later in init()
	init,
	config,
	quit,
};

void config()
{
	DialogBox(plugin.hDllInstance,MAKEINTRESOURCE(IDD_DIALOG1),plugin.hwndParent,ConfigProc);
}

void quit()
{
	if (midi_device_num >= 0)
	{
		midi_close(midi_handle);
	}

	die = 1;
	SetEvent(hEvent);

	config_write();
}

HMIDIIN
midi_open(int device_num, midi_callback callback, ULONG userdata)
{
	HMIDIIN handle;

	if ((midiInOpen(&handle, device_num, (ULONG) callback, userdata, CALLBACK_FUNCTION) == MMSYSERR_NOERROR)
		&& (midiInStart(handle) == MMSYSERR_NOERROR))
	{
		return handle;
	}
	else
	{
		return NULL;
	}
}

BOOL 
midi_close(HMIDIIN handle)
{
	if ((midiInStop(handle) == MMSYSERR_NOERROR)
		&& (midiInClose(handle) == MMSYSERR_NOERROR))
	{
		return TRUE;
	}
	else
	{
		return FALSE;
	}
}

void CALLBACK
midi_data_received(
				   HMIDIIN hMidiIn,  
				   UINT wMsg,		 
				   DWORD dwInstance, 
				   DWORD dwParam1,	 
				   DWORD dwParam2
				   )
{ 
	if ((wMsg != MIM_DATA && wMsg != MIM_LONGDATA) && midi_device_num >= 0)
	{
		return;
	}

	if (wMsg == MIM_DATA)
	{
		// copy midi data to global variable
		MidiData = dwParam1;

		SetEvent(hEvent);
	}

#if 0
	if (wMsg == MIM_LONGDATA)
	{
		// copy midi data to global variable
		MidiData = dwParam1;

		SetEvent(hEvent);
	}
#endif
}

void CALLBACK
midi_data_autoconfig(
				   HMIDIIN hMidiIn,  
				   UINT wMsg,		 
				   DWORD dwInstance, 
				   DWORD dwParam1,	 
				   DWORD dwParam2
				   )
{
	MIDIINCAPS midi_struct;
	HWND hWnd;
	int 	status;
	char num_string[32];

	if (wMsg != MIM_DATA)
	{
		return;
	}

	status = LOBYTE(LOWORD(dwParam1));

	if (status >= NOTE_PRESSED && status < NOTE_PRESSED + 16)
	{
		AutoChannel = status - NOTE_PRESSED;
		AutoDevice = dwInstance;

		hWnd = GetDlgItem(AutoWindow, IDC_DETECTED_PORT);

		midiInGetDevCaps(dwInstance, &midi_struct, sizeof(MIDIINCAPS));
	
		SetWindowText(hWnd, midi_struct.szPname);

		wsprintf(num_string, "%d", AutoChannel+1);

		hWnd = GetDlgItem(AutoWindow, IDC_DETECTED_CHAN);

		SetWindowText(hWnd, num_string);
	}
}

BOOL
is_jitter(int lastvalue, int newvalue)
{
	if (lastvalue != newvalue || lastvalue+1 != newvalue || lastvalue-1 != newvalue)
		return FALSE;
	else
		return TRUE;
}

DWORD WINAPI Thread(
  LPVOID lpParameter
)
{
	int 	status;
	int 	value1;
	int 	value2;
	ULONG	stackMidiData;
	static DWORD tick_count;
		
	while(TRUE)
	{
		WaitForSingleObject(hEvent, INFINITE);

		stackMidiData = MidiData;
		
		status			= LOBYTE(LOWORD(stackMidiData));
		value1			= HIBYTE(LOWORD(stackMidiData));
		value2			= LOBYTE(HIWORD(stackMidiData));

		if (die)
			return 0;

		// limit new messages to 10 per second
		if (tick_count + 50 > GetTickCount())
			continue;
		tick_count = GetTickCount();
		
		if ((status == NOTE_PRESSED + midi_device_channel) && value2 != 0)
		{
			// work in all octaves
			value1 %= 12;
			
			switch (value1)
			{
			case 0:
				SendMessage(plugin.hwndParent,WM_COMMAND,WINAMP_BUTTON1, 0);
				break;
			case 1:
				SendMessage(plugin.hwndParent,WM_COMMAND,WINAMP_OPTIONS_EQ, 0);
				break;
			case 2:
				if (SendMessage(plugin.hwndParent,WM_WA_IPC,0,IPC_ISPLAYING) == 1)
				{
					SendMessage(plugin.hwndParent,WM_COMMAND,WINAMP_BUTTON3, 0);
				}
				else
				{
					SendMessage(plugin.hwndParent,WM_COMMAND,WINAMP_BUTTON2, 0);
				}
				/*SendMessage(plugin.hwndParent,WM_COMMAND,WINAMP_BUTTON2, 0);*/
				break;
			case 3:
				SendMessage(plugin.hwndParent,WM_COMMAND,WINAMP_OPTIONS_PLEDIT, 0);
				break;
			case 4:
				SendMessage(plugin.hwndParent,WM_COMMAND,WINAMP_BUTTON3, 0);
				break;
			case 5:
				SendMessage(plugin.hwndParent,WM_COMMAND,WINAMP_BUTTON4, 0);
				break;
			case 6:
				SendMessage(plugin.hwndParent,WM_COMMAND,WINAMP_FILE_PLAY, 0);
				break;
			case 7:
				SendMessage(plugin.hwndParent,WM_COMMAND,WINAMP_BUTTON5, 0);
				break;
			case 8:
				SendMessage(plugin.hwndParent,WM_COMMAND,WINAMP_FILE_SHUFFLE, 0);
				break;
			case 9:
				SendMessage(plugin.hwndParent,WM_COMMAND,WINAMP_VOLUMEDOWN, 0);
				SendMessage(plugin.hwndParent,WM_COMMAND,WINAMP_VOLUMEDOWN, 0);
				SendMessage(plugin.hwndParent,WM_COMMAND,WINAMP_VOLUMEDOWN, 0);
				SendMessage(plugin.hwndParent,WM_COMMAND,WINAMP_VOLUMEDOWN, 0);
				break;
			case 10:
				SendMessage(plugin.hwndParent,WM_COMMAND,WINAMP_FILE_REPEAT, 0);
				break;
			case 11:
				SendMessage(plugin.hwndParent,WM_COMMAND,WINAMP_VOLUMEUP, 0);
				SendMessage(plugin.hwndParent,WM_COMMAND,WINAMP_VOLUMEUP, 0);
				SendMessage(plugin.hwndParent,WM_COMMAND,WINAMP_VOLUMEUP, 0);
				SendMessage(plugin.hwndParent,WM_COMMAND,WINAMP_VOLUMEUP, 0);
				break;
			default:
				break;
			}
		}
		else if (status == PITCH + midi_device_channel && value1 == PITCH_WHEEL)
		{
			static int lastvalue;

			if (is_jitter(lastvalue, value2))
				continue;
			
			if (value2 > 0x40 && value2 > lastvalue)
			{
				SendMessage(plugin.hwndParent,WM_COMMAND,WINAMP_VOLUMEUP, 0);
			}
			else if (value2 < 0x40 && value2 < lastvalue)
			{
				SendMessage(plugin.hwndParent,WM_COMMAND,WINAMP_VOLUMEDOWN, 0);
			}
			
			lastvalue = value2;
		}
		else if (status == CONTROL + midi_device_channel && value1 == MOD_WHEEL)
		{
			static int lastvalue;

			if (is_jitter(lastvalue, value2))
				continue;

			SendMessage(plugin.hwndParent,WM_WA_IPC,value2 * 2,IPC_SETVOLUME);

			lastvalue = value2;
		}
		else if (status == TASCAM_CONTROL && value2 == TASCAM_ON)
		{
			switch (value1)
			{
			case TASCAM_REW:
				SendMessage(plugin.hwndParent,WM_COMMAND,WINAMP_REW5S, 0);
				SendMessage(plugin.hwndParent,WM_COMMAND,WINAMP_REW5S, 0);
				break;
			case TASCAM_PLAY:
				if (SendMessage(plugin.hwndParent,WM_WA_IPC,0,IPC_ISPLAYING) == 1)
				{
					SendMessage(plugin.hwndParent,WM_COMMAND,WINAMP_BUTTON3, 0);
				}
				else
				{
					SendMessage(plugin.hwndParent,WM_COMMAND,WINAMP_BUTTON2, 0);
				}
				break;
			case TASCAM_STOP:
				SendMessage(plugin.hwndParent,WM_COMMAND,WINAMP_BUTTON4, 0);
				break;
			case TASCAM_FFW:
				SendMessage(plugin.hwndParent,WM_COMMAND,WINAMP_FFWD5S, 0);
				SendMessage(plugin.hwndParent,WM_COMMAND,WINAMP_FFWD5S, 0);
				break;
			case TASCAM_LOCATE_L:
				SendMessage(plugin.hwndParent,WM_COMMAND,WINAMP_BUTTON1, 0);
				break;
			case TASCAM_LOCATE_R:
				SendMessage(plugin.hwndParent,WM_COMMAND,WINAMP_BUTTON5, 0);
				break;
			default:
				break;
			}
		}
		else if (status == TASCAM_CONTROL)
		{
			if (value1 == TASCAM_FADER)
			{
				static int lastvalue;
				
				if (is_jitter(lastvalue, value2))
					continue;
				
				SendMessage(plugin.hwndParent,WM_WA_IPC,value2 * 2,IPC_SETVOLUME);
				
				lastvalue = value2;
			}
#if 0
			if (value1 == TASCAM_DATAWHL)
			{
				if (value2 > 0x40)
				{
					SendMessage(plugin.hwndParent,WM_COMMAND,WINAMP_FFWD5S, 0);
				}
				else if (value2 < 0x40)
				{
					SendMessage(plugin.hwndParent,WM_COMMAND,WINAMP_REW5S, 0);
				}
			}
#endif
		}

	} // end while(1)

	return 0;
}

//
//	Set up/open stuff
//
int 
init()
{
	// Format name of module for display in winamp
	{
		static char c[512];
		char filename[512],*p;
		GetModuleFileName(plugin.hDllInstance,filename,sizeof(filename));
		p = filename+lstrlen(filename);
		while (p >= filename && *p != '\\') p--;
		wsprintf((plugin.description=c),"%s Plug-In v1.1 (%s)",szAppName,p+1);
	}

	config_read();
	
	if (midi_device_num >= 0)
	{
		if (!(midi_handle = midi_open(midi_device_num, midi_data_received, 0)))
		{
			MessageBox(plugin.hwndParent, "Could not open MIDI device", NULL, MB_OK);
		}
	}

	hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

	CreateThread(NULL, 0, Thread, NULL, 0, &ThreadId);
	 
	return 0;
}



BOOL CALLBACK ConfigProc(HWND hwndDlg,UINT uMsg,WPARAM wParam,LPARAM lParam)
{
	switch (uMsg)
	{
		case WM_INITDIALOG:
			{
				int i;
				int num_of_midi_inputs;

				MainDlgWindow = hwndDlg;

				num_of_midi_inputs = midiInGetNumDevs();

				SendDlgItemMessage(hwndDlg, IDC_MIDI_INPUTS, CB_ADDSTRING, 0, (LONG) "None");

				// fill in midi input device names
				for (i = 0; i < num_of_midi_inputs; i++)
				{
					MIDIINCAPS midi_struct;

					midiInGetDevCaps(i, &midi_struct, sizeof(MIDIINCAPS));

					SendDlgItemMessage(hwndDlg, IDC_MIDI_INPUTS, CB_ADDSTRING, 0, (LPARAM) midi_struct.szPname);
				}
				SendDlgItemMessage(hwndDlg, IDC_MIDI_INPUTS, CB_SETCURSEL, midi_device_num + 1, 0);


				// fill in midi channel numbers
				for (i = 0; i < 16; i++)
				{
					char buff[5];
					wsprintf(buff, "%d", i + 1);

					SendDlgItemMessage(hwndDlg, IDC_MIDI_CHANNELS, CB_ADDSTRING, 0, (LPARAM) buff);
				}
				SendDlgItemMessage(hwndDlg, IDC_MIDI_CHANNELS, CB_SETCURSEL, midi_device_channel, 0);
			}
			break;
		case WM_COMMAND:
			switch (LOWORD(wParam))
			{
				case IDC_APPLY:
				case IDOK:

					// if we have an old device open, close it.
					if (midi_device_num >= 0)
					{
						if (!midi_close(midi_handle))
						{
							MessageBox(hwndDlg, szErrorClose, NULL, MB_OK);
						}
					}

					midi_device_num = SendDlgItemMessage(hwndDlg, IDC_MIDI_INPUTS, CB_GETCURSEL, 0, 0);

					midi_device_num--;

					// "None" was selected
					if (midi_device_num == -1)
					{
						// Don't do anything, we're disabled.
					}
					else
					{
						if (!(midi_handle = midi_open(midi_device_num, midi_data_received, 0)))
						{
							MessageBox(hwndDlg, szErrorOpen, NULL, MB_OK);
							midi_device_num = -1;
						}
					}

					midi_device_channel = SendDlgItemMessage(hwndDlg, IDC_MIDI_CHANNELS, CB_GETCURSEL, 0, 0);

				case IDCANCEL:
					if (LOWORD(wParam) != IDC_APPLY) EndDialog(hwndDlg,0);
					break;
				case IDC_AUTODETECT:
					// if we have an old device open, close it.
					if (midi_device_num >= 0)
					{
						if (!midi_close(midi_handle))
						{
							MessageBox(hwndDlg, szErrorClose, NULL, MB_OK);
						}
						midi_device_num = -1;
					}
					DialogBox(plugin.hDllInstance,MAKEINTRESOURCE(IDD_AUTODETECT),plugin.hwndParent,AutodetectProc);
					break;
			}
	}
	return FALSE;
}

BOOL CALLBACK AutodetectProc(HWND hwndDlg,UINT uMsg,WPARAM wParam,LPARAM lParam)
{
	int i;
	static int num_of_midi_inputs;

	switch (uMsg)
	{
		case WM_INITDIALOG:
			{
				AutoWindow = hwndDlg;
				AutoDevice = -1;
				AutoChannel = -1;

				num_of_midi_inputs = midiInGetNumDevs();

				MidiDevArray = GlobalAlloc (0, num_of_midi_inputs * sizeof (HMIDIIN));

				// fill in midi input device names
				for (i = 0; i < num_of_midi_inputs; i++)
				{
					MidiDevArray[i] = midi_open(i, midi_data_autoconfig, i);
				}
			}
			break;
		case WM_COMMAND:
			switch (LOWORD(wParam))
			{
				case IDC_APPLY:
				case IDOK:

					if (AutoDevice != -1)
					{
						SendDlgItemMessage(MainDlgWindow, IDC_MIDI_INPUTS, CB_SETCURSEL, AutoDevice + 1, 0);
						SendDlgItemMessage(MainDlgWindow, IDC_MIDI_CHANNELS, CB_SETCURSEL, AutoChannel, 0);
					}

				case IDCANCEL:

					for (i = 0; i < num_of_midi_inputs; i++)
					{
						midi_close(MidiDevArray[i]);
					}

					GlobalFree(MidiDevArray);

					if (LOWORD(wParam) != IDC_APPLY) EndDialog(hwndDlg,0);
					break;
			}
	}
	return FALSE;
}




void config_read()
{
	char ini_file[MAX_PATH], *p;
	GetModuleFileName(plugin.hDllInstance,ini_file,sizeof(ini_file));
	p=ini_file+lstrlen(ini_file);
	while (p >= ini_file && *p != '\\') p--;
	if (++p >= ini_file) *p = 0;
	lstrcat(ini_file,"plugin.ini");

	midi_device_num = GetPrivateProfileInt(szAppName,"MIDIDevice",midi_device_num,ini_file);
	midi_device_channel = GetPrivateProfileInt(szAppName,"MIDIChannel",midi_device_channel,ini_file);
}

void config_write()
{
	char ini_file[MAX_PATH], *p;
	char string[32];
	GetModuleFileName(plugin.hDllInstance,ini_file,sizeof(ini_file));
	p=ini_file+lstrlen(ini_file);
	while (p >= ini_file && *p != '\\') p--;
	if (++p >= ini_file) *p = 0;
	lstrcat(ini_file,"plugin.ini");

	wsprintf(string,"%d",midi_device_num);
	WritePrivateProfileString(szAppName,"MIDIDevice",string,ini_file);
	
	wsprintf(string,"%d",midi_device_channel);
	WritePrivateProfileString(szAppName,"MIDIChannel",string,ini_file);
}

__declspec( dllexport ) winampGeneralPurposePlugin * winampGetGeneralPurposePlugin()
{
	return &plugin;
}

int main()
{
	// do nothing
	return 0;
}
