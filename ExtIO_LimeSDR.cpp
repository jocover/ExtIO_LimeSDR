/*
The MIT License

Copyright (c) 2017 Jiang Wei  <jiangwei@jiangwei.org>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <iostream>
#include <string>
#include <thread>
#include <LimeSuite.h>
#include "ExtIO_LimeSDR.h"
#include "resource.h"

#define HWNAME				"ExtIO LimeSDR"
#define BUF_SIZE  4096 /* must be multiple of 512 */


HWND h_dialog = nullptr;

lms_stream_t streamId;
bool running = false;
bool transmitter = false;
int16_t* buffer = nullptr;
std::thread thread;

lms_device_t * device = nullptr;
pfnExtIOCallback	Callback = nullptr;

typedef struct sr {
	float_type value;
	TCHAR *name;
} sr_t;

static sr_t samplerates[] = {
	{  2000000, TEXT("2 MSPS") },
	{  4000000, TEXT("4 MSPS") },
	{  8000000, TEXT("8 MSPS") },
	{ 10000000, TEXT("10 MSPS") },
	{ 15000000, TEXT("15 MSPS") },
	{ 20000000, TEXT("20 MSPS") },
	{ 25000000, TEXT("25 MSPS") },
	{ 30000000, TEXT("30 MSPS") },
	{ 40000000, TEXT("40 MSPS") }
};

size_t channel = 0;

int sr_idx = 1;

int ant_select = 2;

uint16_t pga = 0;
uint16_t tia = 0;
uint16_t lna = 0;

size_t gain = 35;


//---------------------------------------------------------------------------

void error() {

	MessageBox(NULL, LMS_GetLastErrorMessage(), NULL, MB_OK);
	if (device != NULL)
		LMS_Close(device);
}

void RecvThread() {

	while (running) {

		int samplesRead = LMS_RecvStream(&streamId, buffer, BUF_SIZE, NULL, 1000);

		if (Callback != nullptr) {

			Callback(samplesRead * 2, 0, 0, buffer);
		}

	}

}

#if BORLAND
#pragma argsused
BOOL WINAPI DllMain(HINSTANCE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)

#else

HMODULE hInst;

BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
)
#endif
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		hInst = hModule;
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;

	}
	return TRUE;
}

static INT_PTR CALLBACK MainDlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
	case WM_INITDIALOG: {

		for (int i = 0; i < (sizeof(samplerates) / sizeof(samplerates[0])); i++) {
			ComboBox_AddString(GetDlgItem(hwndDlg, IDC_SAMPLERATE), samplerates[i].name);
		}

		ComboBox_AddString(GetDlgItem(hwndDlg, IDC_ANT), "NONE");
		ComboBox_AddString(GetDlgItem(hwndDlg, IDC_ANT), "LNA_H");
		ComboBox_AddString(GetDlgItem(hwndDlg, IDC_ANT), "LNA_L");
		ComboBox_AddString(GetDlgItem(hwndDlg, IDC_ANT), "LNA_W");

		Static_SetText(GetDlgItem(hwndDlg, IDC_LIB_VER), LMS_GetLibraryVersion());


		SendDlgItemMessage(hwndDlg, IDC_SLIDER_LNA, TBM_SETRANGEMIN, FALSE, 0);
		SendDlgItemMessage(hwndDlg, IDC_SLIDER_LNA, TBM_SETRANGEMAX, FALSE, 14);
		for (int i = 0; i < 14; i++) {
			SendDlgItemMessage(hwndDlg, IDC_SLIDER_LNA, TBM_SETTIC, FALSE, i);
		}


		SendDlgItemMessage(hwndDlg, IDC_SLIDER_TIA, TBM_SETRANGEMIN, FALSE, 1);
		SendDlgItemMessage(hwndDlg, IDC_SLIDER_TIA, TBM_SETRANGEMAX, FALSE, 3);
		for (int i = 0; i < 2; i++) {
			SendDlgItemMessage(hwndDlg, IDC_SLIDER_TIA, TBM_SETTIC, FALSE, i);
		}


		SendDlgItemMessage(hwndDlg, IDC_SLIDER_PGA, TBM_SETRANGEMIN, FALSE, 0);
		SendDlgItemMessage(hwndDlg, IDC_SLIDER_PGA, TBM_SETRANGEMAX, FALSE, 31);
		for (int i = 0; i < 31; i++) {
			SendDlgItemMessage(hwndDlg, IDC_SLIDER_PGA, TBM_SETTIC, FALSE, i);
		}

		return TRUE;

	}
						break;
	case WM_HSCROLL:
		if (GetDlgItem(hwndDlg, IDC_SLIDER_LNA) == (HWND)lParam) {

			if (LOWORD(wParam) != TB_THUMBTRACK && LOWORD(wParam) != TB_ENDTRACK) {

				if (lna != SendDlgItemMessage(hwndDlg, IDC_SLIDER_LNA, TBM_GETPOS, 0, NULL)) {
					lna = SendDlgItemMessage(hwndDlg, IDC_SLIDER_LNA, TBM_GETPOS, 0, NULL);
					std::string lna_value = std::to_string(lna>7?(lna - 14): (lna - 10)*3);
					lna_value.append(" dB");
					Static_SetText(GetDlgItem(hwndDlg, IDC_LNA_VALUE), lna_value.c_str());

					if (lna <= 14)
						LMS_WriteParam(device, LMS7param(G_LNA_RFE), lna);

				}

			}

		}
		if (GetDlgItem(hwndDlg, IDC_SLIDER_TIA) == (HWND)lParam) {
			if (LOWORD(wParam) != TB_THUMBTRACK && LOWORD(wParam) != TB_ENDTRACK) {

				if (tia != SendDlgItemMessage(hwndDlg, IDC_SLIDER_TIA, TBM_GETPOS, 0, NULL)) {
					tia = SendDlgItemMessage(hwndDlg, IDC_SLIDER_TIA, TBM_GETPOS, 0, NULL);
					std::string tia_value = std::to_string((tia==3)?0: (tia == 2) ? -3:-12);
					tia_value.append(" dB");
					Static_SetText(GetDlgItem(hwndDlg, IDC_TIA_VALUE), tia_value.c_str());

					if (tia <= 3)
						LMS_WriteParam(device, LMS7param(G_TIA_RFE), tia);
				}

			}

		}
		if (GetDlgItem(hwndDlg, IDC_SLIDER_PGA) == (HWND)lParam) {
			if (LOWORD(wParam) != TB_THUMBTRACK && LOWORD(wParam) != TB_ENDTRACK) {

				if (pga != SendDlgItemMessage(hwndDlg, IDC_SLIDER_PGA, TBM_GETPOS, 0, NULL)) {
					pga = SendDlgItemMessage(hwndDlg, IDC_SLIDER_PGA, TBM_GETPOS, 0, NULL);
					std::string pga_value = std::to_string(pga - 11);
					pga_value.append(" dB");
					Static_SetText(GetDlgItem(hwndDlg, IDC_PGA_VALUE), pga_value.c_str());

					int rcc_ctl_pga_rbb = (430 * (pow(0.65, ((double)pga / 10))) - 110.35) / 20.4516 + 16;

					if (tia <= 31) {
						LMS_WriteParam(device, LMS7param(G_PGA_RBB), pga);
						LMS_WriteParam(device, LMS7param(RCC_CTL_PGA_RBB), rcc_ctl_pga_rbb);
					}

				}

			}

		}
		break;
	case WM_SHOWWINDOW: {

		ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_SAMPLERATE), sr_idx);
		ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_ANT), ant_select);

		LMS_ReadParam(device, LMS7param(G_PGA_RBB), &pga);
		LMS_ReadParam(device, LMS7param(G_TIA_RFE), &tia);
		LMS_ReadParam(device, LMS7param(G_LNA_RFE), &lna);

		SendDlgItemMessage(hwndDlg, IDC_SLIDER_LNA, TBM_SETPOS, TRUE, (int)lna);

		std::string lna_value = std::to_string(lna>7 ? (lna - 14) : (lna - 10) * 3);
		lna_value.append(" dB");
		Static_SetText(GetDlgItem(hwndDlg, IDC_LNA_VALUE), lna_value.c_str());

		SendDlgItemMessage(hwndDlg, IDC_SLIDER_TIA, TBM_SETPOS, TRUE, (int)tia);

		std::string tia_value = std::to_string((tia == 3) ? 0 : (tia == 2) ? -3 : -12);
		tia_value.append(" dB");
		Static_SetText(GetDlgItem(hwndDlg, IDC_TIA_VALUE), tia_value.c_str());

		SendDlgItemMessage(hwndDlg, IDC_SLIDER_PGA, TBM_SETPOS, TRUE, (int)pga);
		std::string pga_value = std::to_string(pga - 12);
		pga_value.append(" dB");
		Static_SetText(GetDlgItem(hwndDlg, IDC_PGA_VALUE), pga_value.c_str());

		if (channel == 0) {
			Button_SetCheck(GetDlgItem(hwndDlg, IDC_RX1), BST_CHECKED);
			Button_SetCheck(GetDlgItem(hwndDlg, IDC_RX2), BST_UNCHECKED);
		}
		else {
			Button_SetCheck(GetDlgItem(hwndDlg, IDC_RX1), BST_UNCHECKED);
			Button_SetCheck(GetDlgItem(hwndDlg, IDC_RX2), BST_CHECKED);
		}

		return TRUE;
	}

						break;

	case WM_COMMAND:
		switch (GET_WM_COMMAND_ID(wParam, lParam)) {
		case IDC_SAMPLERATE: {
			if (GET_WM_COMMAND_CMD(wParam, lParam) == CBN_SELCHANGE) {
				int64_t freq = 0;
				sr_idx = ComboBox_GetCurSel(GET_WM_COMMAND_HWND(wParam, lParam));

				if (running) {
					freq = GetHWLO64();
					StopHW();
				}

				if (LMS_SetSampleRate(device, samplerates[sr_idx].value, 2) != 0) {
					error();
				}


				if (freq != 0) {

					StartHW64(freq);
				}

				Callback(-1, extHw_Changed_SampleRate, 0, NULL);

			}

			return TRUE;
		}
							 break;

		case IDC_ANT: {
			if (GET_WM_COMMAND_CMD(wParam, lParam) == CBN_SELCHANGE) {
				ant_select = ComboBox_GetCurSel(GET_WM_COMMAND_HWND(wParam, lParam));

				if (LMS_SetAntenna(device, LMS_CH_RX, channel, ant_select) != 0)
					error();

			}

			return TRUE;
		}
					  break;
		case IDC_RX1: {
			if (Button_GetCheck(GET_WM_COMMAND_HWND(wParam, lParam)) == BST_CHECKED) {
				Button_SetCheck(GetDlgItem(hwndDlg, IDC_RX2), BST_UNCHECKED);

				if (channel == 0)
					return TRUE;

				channel = 0;

				if (LMS_EnableChannel(device, LMS_CH_RX, channel, true) != 0)
					error();

				if (LMS_SetAntenna(device, LMS_CH_RX, channel, ant_select) != 0) {
					error();
				}

				if (LMS_SetNormalizedGain(device, LMS_CH_RX, channel, gain / 70.0) != 0) {
					error();
				}

				if (running) {

					int64_t freq = GetHWLO64();
					StopHW();
					StartHW64(freq);
				}
			}

			return TRUE;
		}
					  break;
		case IDC_RX2: {
			if (Button_GetCheck(GET_WM_COMMAND_HWND(wParam, lParam)) == BST_CHECKED) {
				Button_SetCheck(GetDlgItem(hwndDlg, IDC_RX1), BST_UNCHECKED);

				if (channel == 1)
					return TRUE;

				channel = 1;

				if (LMS_EnableChannel(device, LMS_CH_RX, channel, true) != 0)
					error();

				if (LMS_SetAntenna(device, LMS_CH_RX, channel, ant_select) != 0) {
					error();
				}

				if (LMS_SetNormalizedGain(device, LMS_CH_RX, channel, gain / 70.0) != 0) {
					error();
				}

				if (running) {

					int64_t freq = GetHWLO64();
					StopHW();
					StartHW64(freq);
				}
			}

			return TRUE;
		}
					  break;

		}
		break;
	case WM_CLOSE:
		ShowWindow(h_dialog, SW_HIDE);
		return TRUE;
		break;
	case WM_DESTROY:
		h_dialog = NULL;
		return TRUE;
		break;

	}

	return FALSE;
}

extern "C"
bool __declspec(dllexport) __stdcall InitHW(char *name, char *model, int& type)
{
	int n;
	lms_device_t * _dev = nullptr;
	lms_info_str_t list[8];
	if ((n = LMS_GetDeviceList(list)) < 1)
		return false;

	if (LMS_Open(&_dev, list[0], NULL)) {
		return false;
	}

	const lms_dev_info_t* info = LMS_GetDeviceInfo(_dev);

	type = exthwUSBdata16;
	strcpy(name, HWNAME);
	strcpy(model, info->deviceName);

	LMS_Close(_dev);

	return true;
}

extern "C"
bool EXTIO_API OpenHW(void)
{

	int n;
	lms_info_str_t list[8];
	if ((n = LMS_GetDeviceList(list)) < 1) {
		error();
		return false;
	}


	if (LMS_Open(&device, list[0], NULL)) {
		error();
		return false;
	}

	if (LMS_Init(device) != 0) {
		error();
		return false;
	}

	if (LMS_EnableChannel(device, LMS_CH_RX, channel, true) != 0)
		error();

	if (LMS_SetSampleRate(device, samplerates[sr_idx].value, 2) != 0) {
		error();
		return false;
	}


	if (LMS_SetAntenna(device, LMS_CH_RX, channel, ant_select) != 0) {
		error();
		return false;
	}


	if (LMS_SetNormalizedGain(device, LMS_CH_RX, channel, gain / 70.0) != 0) {
		error();
		return false;
	}


	h_dialog = CreateDialog(hInst, MAKEINTRESOURCE(IDD_LIMESDR_SETTINGS), NULL, (DLGPROC)MainDlgProc);
	ShowWindow(h_dialog, SW_HIDE);

	buffer = new (std::nothrow) int16_t[BUF_SIZE * 2];
	

	return true;
}


extern "C"
void EXTIO_API ShowGUI()
{
	ShowWindow(h_dialog, SW_SHOW);
	SetForegroundWindow(h_dialog);
	return;
}

extern "C"
void EXTIO_API HideGUI()
{
	ShowWindow(h_dialog, SW_HIDE);
	return;
}

extern "C"
void EXTIO_API SwitchGUI()
{
	if (IsWindowVisible(h_dialog))
		ShowWindow(h_dialog, SW_HIDE);
	else
		ShowWindow(h_dialog, SW_SHOW);
	return;
}

extern "C"
int  EXTIO_API StartHW(long LOfreq)
{
	int64_t ret = StartHW64((int64_t)LOfreq);
	return (int)ret;
}

extern "C"
int64_t  EXTIO_API StartHW64(int64_t LOfreq)
{

	SetHWLO64(LOfreq);

	streamId.channel = channel;
	streamId.fifoSize = 1024 * 128;
	streamId.throughputVsLatency = 1.0;
	streamId.isTx = false;
	streamId.dataFmt = lms_stream_t::LMS_FMT_I16;

	if (LMS_SetupStream(device, &streamId) != 0)
		error();

	LMS_StartStream(&streamId);

	running = true;

	thread = std::thread(RecvThread);

	return BUF_SIZE;
}

extern "C"
void EXTIO_API StopHW(void)
{
	if (running) {

		running = false;
		thread.detach();

		LMS_StopStream(&streamId);
		LMS_DestroyStream(device, &streamId);

	}

}

extern "C"
void EXTIO_API CloseHW(void)
{

	LMS_Close(device);

	if (h_dialog != NULL)
		DestroyWindow(h_dialog);

	delete buffer;
	
}

extern "C"
int  EXTIO_API SetHWLO(long LOfreq)
{

	int64_t ret = SetHWLO64((int64_t)LOfreq);
	return (ret & 0xFFFFFFFF);

}

extern "C"
int64_t  EXTIO_API SetHWLO64(int64_t LOfreq)
{
	int64_t ret = 0;
	bool restart = false;
	float_type freq = float_type(LOfreq);
	if (LOfreq < 1e3) {
		freq = 1e3;
		ret = -1 * (1e3);
	}

	if (LOfreq > 3800e6) {
		freq = 3800e6;
		ret = 3800e6;
	}

	if (running && freq < 30e6) {

		StopHW();
		StartHW64(freq);

	}
	else {

		if (LMS_SetLOFrequency(device, LMS_CH_RX, channel, freq) == 0) {
			Callback(-1, extHw_Changed_LO, 0, NULL);
		}

	}

	return ret;
}

extern "C"
int  EXTIO_API GetStatus(void)
{
	return 0;
}

extern "C"
void EXTIO_API SetCallback(pfnExtIOCallback funcptr)
{
	Callback = funcptr;
	return;
}

extern "C"
int64_t EXTIO_API GetHWLO64(void)
{
	float_type freq;
	LMS_GetLOFrequency(device, LMS_CH_RX, channel, &freq);
	return int64_t(freq);
}

extern "C"
long EXTIO_API GetHWLO(void)
{
	int64_t	glLOfreq = GetHWLO64();
	return (long)(glLOfreq & 0xFFFFFFFF);
}

extern "C"
long EXTIO_API GetHWSR(void)
{

	long SampleRate;
	if (device != nullptr) {
		float_type freq;

		if (LMS_GetSampleRate(device, LMS_CH_RX, channel, &freq, nullptr) != 0)
			error();

		SampleRate = ceil(freq);
	}

	return SampleRate;
}

extern "C"
void EXTIO_API VersionInfo(const char * progname, int ver_major, int ver_minor)
{

}

extern "C"
int EXTIO_API GetAttenuators(int atten_idx, float * attenuation)
{
	if (atten_idx < 70) {
		*attenuation = atten_idx;
		return 0;
	}
	return 1;
}

extern "C"
int EXTIO_API GetActualAttIdx(void)
{
	float_type _gain;
	if (device != nullptr) {
		if (LMS_GetNormalizedGain(device, LMS_CH_RX, channel, &_gain) != 0)
			error();
	}
	return ceil(_gain * 70);
}

extern "C"
int EXTIO_API SetAttenuator(int atten_idx)
{
	gain = atten_idx;
	if (device != nullptr) {
		if (LMS_SetNormalizedGain(device, LMS_CH_RX, channel, gain / 70.0) != 0)
			error();
		return 0;
	}
	return 1;
}

extern "C"
int EXTIO_API ExtIoGetSrates(int srate_idx, double * samplerate)
{
	if (srate_idx < (sizeof(samplerates) / sizeof(samplerates[0])))
	{
		*samplerate = samplerates[srate_idx].value;
		return 0;
	}
	else {
		return -1;
	}
}

extern "C"
int  EXTIO_API ExtIoGetActualSrateIdx(void)
{
	return  sr_idx;
}

extern "C"
int  EXTIO_API ExtIoSetSrate(int srate_idx)
{
	if (srate_idx >= 0 && srate_idx < (sizeof(samplerates) / sizeof(samplerates[0])))
	{
		int64_t freq = 0;

		if (running) {
			freq = GetHWLO64();
			StopHW();
		}

		if (LMS_SetSampleRate(device, samplerates[srate_idx].value, 2) != 0) {
			error();
		}


		if (freq != 0) {
			StartHW64(freq);
		}
		sr_idx = srate_idx;
		Callback(-1, extHw_Changed_SampleRate, 0, NULL);// Signal application
		return 0;
	}
	
	return 1;	// ERROR
}

extern "C"
int  EXTIO_API ExtIoGetSetting(int idx, char * description, char * value)
{
	switch (idx) {
	case 0: {
		description = "SampleRateIdx";
		_itoa(sr_idx, value, 1024);
		return 0;
	}
			break;

	case 1: {
		description = "Channel";
		_itoa(channel, value, 1024);
		return 0;
	}
			break;

	case 2: {
		description = "Antenna";
		_itoa(ant_select, value, 1024);
		return 0;
	}
			break;

	case 3: {
		description = "LNA";
		_itoa(lna, value, 1024);
		return 0;
	}
			break;

	case 4: {
		description = "PGA";
		_itoa(pga, value, 1024);
		return 0;
	}
			break;

	case 5: {
		description = "TIA";
		_itoa(tia, value, 1024);
		return 0;
	}
			break;


	}
	return -1;
}

extern "C"
void EXTIO_API ExtIoSetSetting(int idx, const char * value)
{
	int tempInt;

	switch (idx)
	{
	case 0: {
		tempInt = atoi(value);

		if (tempInt >= 0 && tempInt < (sizeof(samplerates) / sizeof(samplerates[0])))
		{
			sr_idx = tempInt;
		}
	}
			break;

	case 1: {
		tempInt = atoi(value);

		if (tempInt < 2 && tempInt >= 0)
		{
			channel = tempInt;
		}
	}
			break;

	case 2: {
		tempInt = atoi(value);

		if (tempInt < 4 && tempInt >= 0)
		{
			ant_select = tempInt;
		}
	}
			break;

	case 3: {
		tempInt = atoi(value);

		if (tempInt < 15 && tempInt >= 0)
		{
			lna = tempInt;
		}
	}
			break;

	case 4: {
		tempInt = atoi(value);

		if (tempInt < 32 && tempInt >= 0)
		{
			pga = tempInt;
		}
	}
			break;

	case 5: {
		tempInt = atoi(value);

		if (tempInt < 3 && tempInt >= 0)
		{
			tia = tempInt;
		}
	}
			break;


	}
}

extern "C"
int EXTIO_API ActivateTx(int magicA, int magicB) {

	if (magicA != -1 || magicB != -1) {
		//code from ExtIO_Si570.dll
		Callback((magicA ^ 0xA85EF5E1) + magicB + 0x6D276F, (magicB ^ 0x57A10A1E) + magicA + 0x3F5005D, 0.0, 0);
	}
	return 0;
}

extern "C"
void ModeChanged(char mode) {
	//‘A’  =  AM ‘E’  =  ECSS ‘F’  = FM ‘L’  =  LSB ‘U’  =  USB ‘C’  =  CW ‘D’  =  DRM
}


extern "C"
int EXTIO_API SetModeRxTx(int modeRxTx) {

	//mode 0 rx  mode 1 tx
	return 0;
}

extern "C"
void EXTIO_API TxSamples(int status, int numIQsamples, const short * interleavedIQ) {


}