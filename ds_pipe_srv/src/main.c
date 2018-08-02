#include <Windows.h>
#include "dscaler.h"
#include "helpids.h"
#include <stdlib.h>


__declspec(dllexport)   FILTER_METHOD* GetFilterPluginInfo( long CpuFeatureFlags );

void SendUDPPacket( DWORD* pHistogram, TDeinterlaceInfo* pInfo, DWORD Color, DWORD EndColor, DWORD LowValue, DWORD HighValue );
LONG __cdecl            SendPacket( TDeinterlaceInfo* pInfo );
void __cdecl init(void);
void __cdecl dispose(void);

FILTER_METHOD SendUDPMethod =
{
    sizeof(FILTER_METHOD),
    FILTER_CURRENT_VERSION,
    DEINTERLACE_INFO_CURRENT_VERSION,
    "Pipe Out Filter",
    "Pipe Out",
    FALSE,		// Are we active Initially FALSE
    TRUE,		// Do we get called on Input
    SendPacket,	// Pointer to Algorithm function (cannot be NULL)
    0,			// id of menu to display status
    TRUE,		// Always run - do we run if there has been an overrun
    init,		// call this if plugin needs to do anything before it is used
    dispose,		// call this if plugin needs to deallocate anything
    NULL,		// Used to save the module Handle
    0,			// number of settings
    NULL,		// pointer to start of Settings[nSettings]
    WM_FLT_HISTOGRAM_GETVALUE - WM_APP,	// the offset used by the external settings API
    TRUE,		// BOOL CanDoInterlaced
    2,			// How many pictures of history do we need to run
    IDH_HISTOGRAM,	// Help ID
};

__declspec(dllexport) FILTER_METHOD* GetFilterPluginInfo( long CpuFeatureFlags )
{
    return &SendUDPMethod;
}


BOOL WINAPI DllMain( HANDLE hInst, ULONG ul_reason_for_call, LPVOID lpReserved )
{
    return TRUE;
}

HANDLE hPipe = INVALID_HANDLE_VALUE;
LPTSTR lpszPipename = TEXT("\\\\.\\pipe\\test");
HANDLE readyFlag = INVALID_HANDLE_VALUE;
void * frame = NULL;
DWORD frsz = 0;
frame_header_t hdr;
BOOL terminateListener = FALSE;
BOOL terminated = FALSE;


DWORD WINAPI ListenPipeProc(LPVOID lpvParam) {
	BOOL conres;
	DWORD res,written;
con:

	hPipe = CreateNamedPipe( 
          lpszPipename,             // pipe name 
          PIPE_ACCESS_OUTBOUND |       // read/write access 
		  FILE_FLAG_FIRST_PIPE_INSTANCE,

          PIPE_TYPE_BYTE |       // message type pipe 
          PIPE_READMODE_BYTE |   // message-read mode 
          PIPE_WAIT,                // blocking mode 

          1, // max. instances  

          1*1024*1024,                  // output buffer size - 1M

          0,                  // input buffer size 
          100,                        // client time-out 
          NULL);                    // default security attribute 

	if(hPipe == INVALID_HANDLE_VALUE)
		return 80;

	conres = ConnectNamedPipe(hPipe, NULL); // wait for client to connect

	if (!conres)
		return 85;


// подключились.. ждем данных
	do {
		if (terminateListener)
			break;
		res = WaitForSingleObject(readyFlag , INFINITE);
		if (res != WAIT_OBJECT_0)
			return 94;

		WriteFile(hPipe, &hdr, sizeof(hdr),&written,NULL);
		if (written != sizeof(hdr))
			break;
		WriteFile(hPipe, frame, frsz, &written, NULL);
		if (written != frsz)
			break;

		ResetEvent(readyFlag);

	} while(TRUE);

	FlushFileBuffers(hPipe); 
	DisconnectNamedPipe(hPipe); 
	CloseHandle(hPipe);

	if (!terminateListener)
		goto con;
	terminated = TRUE;
	return 0;
}


HANDLE hThread = INVALID_HANDLE_VALUE;

void __cdecl init(void) {

	DWORD  dwThreadId = 0; 

	readyFlag = CreateEvent(NULL,TRUE,FALSE,NULL);

	hThread = CreateThread( 
            NULL,              // no security attribute 
            0,                 // default stack size 
            ListenPipeProc,    // thread proc
            NULL,    // thread parameter 
            0,                 // not suspended 
            &dwThreadId);      // returns thread ID 
	if(hThread == INVALID_HANDLE_VALUE)
		return;
}

void __cdecl dispose(void) {

	terminateListener = TRUE;
	Sleep(100);
	if (!terminated) {
		TerminateThread(hThread, 142);
	}
	if (frame != NULL)
		free(frame);
}

void _deinterlace(TDeinterlaceInfo* pInfo, char *src, char * dst, int shift) {
    int i;
    dst += shift;
	for(i = 0; i < pInfo->FieldHeight; i++) {

		pInfo->pMemcpy(dst, src, pInfo->LineLength);

		src += pInfo->InputPitch;

		dst += pInfo->LineLength;
		dst += pInfo->LineLength;
	}
}

void deinterlace(TDeinterlaceInfo* pInfo, char * dst) {
	if (!pInfo->PictureHistory[0]->pData)
		return;
    _deinterlace(pInfo, pInfo->PictureHistory[0]->pData,dst,0);
	if (!pInfo->PictureHistory[1]->pData)
		return;
	_deinterlace(pInfo, pInfo->PictureHistory[1]->pData,dst, pInfo->LineLength);
}


LONG __cdecl SendPacket( TDeinterlaceInfo* pInfo )
{
	BYTE* data;
	DWORD res,size;

    if (pInfo->PictureHistory[0] == NULL || pInfo->PictureHistory[0]->pData == NULL)
    {
        return 1000;
    }


	res = WaitForSingleObject(readyFlag , 0);
	// если флаг готовности установлен - пропусти фрейм
	if (res == WAIT_OBJECT_0) {
		return 1000;
	}
	
	hdr.magic = 0xF00DBEAF;
	hdr.magic2 = 0xDEADBEAF;
	hdr.info = *pInfo;

	size = pInfo->FrameHeight * pInfo->LineLength;

	if (frame != NULL && frsz != size) {
		free(frame);
		frame = NULL;
	}

	if (frame == NULL) {
		frsz = size;
		frame = malloc(frsz);
	}

	if (pInfo->FieldHeight == pInfo->FrameHeight) {

		data = pInfo->PictureHistory[0]->pData;

		pInfo->pMemcpy(frame,data,frsz);
	} else {
	// если это deinterlaced - приложим деинтерлейсер
		deinterlace(pInfo, frame);

		hdr.info.FieldHeight = pInfo->FrameHeight;
		hdr.info.InputPitch = pInfo->LineLength;
	}



	SetEvent(readyFlag);

    return 1000;
}
