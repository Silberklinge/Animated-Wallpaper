#include <cstdint>
#include <iostream>
#include <Windows.h>
#include <d3d11.h>

#include "opencv2/opencv.hpp"

#define CHECK_HR(_hr) { HRESULT hr = (_hr); if( FAILED(hr) ) { wprintf( L"'" L#_hr L"' failed with error code 0x%08lx\n", hr ); std::exit(1); } }

template <typename T>
void retrieve_desktop_settings(T* w, T* h, T* refresh_rate) {
	DEVMODE lpDevMode;
	ZeroMemory(&lpDevMode, sizeof(lpDevMode));
	lpDevMode.dmSize = sizeof(DEVMODE);
	lpDevMode.dmDriverExtra = 0;
	if (EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &lpDevMode) == 0) {
		std::cerr << "Could not retrieve current display settings!" << std::endl;
		std::exit(0);
	}
	*w = static_cast<T>(lpDevMode.dmPelsWidth);
	*h = static_cast<T>(lpDevMode.dmPelsHeight);
	*refresh_rate = static_cast<T>(lpDevMode.dmDisplayFrequency);
}

void get_wallpaper_handle(HWND* hwnd) {
	struct callback_struct {
		static BOOL CALLBACK EnumWindowsCallback(_In_ HWND hwnd, _In_ LPARAM lParam) {
			HWND shelldll_defview = FindWindowEx(hwnd, NULL, "SHELLDLL_DefView", NULL);
			if (shelldll_defview != NULL) {
				auto handle = reinterpret_cast<HWND*>(lParam);
				*handle = FindWindowEx(NULL, hwnd, "WorkerW", NULL);
			}
			return TRUE;
		}
	};

	HWND progman = FindWindowA("Progman", NULL);
	DWORD_PTR result;
	SendMessageTimeoutA(progman, 0x052C, NULL, NULL, SMTO_NORMAL, 1000, &result);
	EnumWindows(callback_struct::EnumWindowsCallback, reinterpret_cast<LPARAM>(hwnd));
}

class Timer {
	double freq;
	LARGE_INTEGER begin;
	LARGE_INTEGER end;

public:
	Timer() {
		LARGE_INTEGER li;
		if (!QueryPerformanceFrequency(&li)) {
			std::cerr << "QueryPerformanceFrequency failed!" << std::endl;
			std::exit(1);
		}

		freq = static_cast<double>(li.QuadPart) / 1000;
	}

	void start() {
		QueryPerformanceCounter(&begin);
	}

	void stop() {
		QueryPerformanceCounter(&end);
	}

	double elapsed_ms() const {
		return (end.QuadPart - begin.QuadPart) / freq;
	}

	void wait(double ms) {
		LONGLONG time = static_cast<LONGLONG>(freq * ms);
		QueryPerformanceCounter(&begin);
		end.QuadPart = begin.QuadPart + time;
		while(begin.QuadPart < end.QuadPart)
			QueryPerformanceCounter(&begin);
	}
};

class VideoRenderer {
	HWND handle;
	HDC device;
	DWORD dst_w;
	DWORD dst_h;

public:
	VideoRenderer(HWND handle, DWORD dst_w, DWORD dst_h) : handle(handle), dst_w(dst_w), dst_h(dst_h) {
		device = GetDCEx(handle, NULL, DCX_WINDOW | DCX_CACHE | DCX_LOCKWINDOWUPDATE);
	}

	void render(const cv::Mat& image) {
		BITMAPINFOHEADER bi;
		bi.biSize = sizeof(BITMAPINFOHEADER);
		bi.biWidth = image.cols;
		bi.biHeight = -image.rows;
		bi.biBitCount = (WORD)(image.channels() * 8);
		bi.biPlanes = 1;
		bi.biCompression = BI_RGB;

		SetDIBitsToDevice(device, 0, 0, dst_w, dst_h, 0, 0, 0, image.rows, image.data, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);
	}

	~VideoRenderer() {
		ReleaseDC(handle, device);
	}
};

/*
class D3D11Renderer {
	template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

	HWND								window;
	ComPtr<ID3D11Device>				d3d11_device;
	ComPtr<ID3D11DeviceContext>			d3d11_device_ctx;
	ComPtr<IDXGISwapChain>				dxgi_swap_chain;

	ComPtr<ID3D11RenderTargetView>		back_buffer;

public:
	D3D11Renderer(HWND hwnd) : window(hwnd) {
		DXGI_SWAP_CHAIN_DESC scd;
		ZeroMemory(&scd, sizeof(DXGI_SWAP_CHAIN_DESC));
		scd.BufferCount = 1;									// one back buffer
		scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;		// use 32-bit color
		scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;		// how swap chain is to be used
		scd.OutputWindow = window;								// the window to be used
		scd.SampleDesc.Count = 4;                               // how many multisamples
		scd.Windowed = TRUE;                                   // windowed/full-screen mode
		D3D11CreateDeviceAndSwapChain(NULL,
			D3D_DRIVER_TYPE_HARDWARE,
			NULL,
			NULL,
			NULL,
			NULL,
			D3D11_SDK_VERSION,
			&scd,
			&dxgi_swap_chain,
			&d3d11_device,
			NULL,
			&d3d11_device_ctx
		);



		// back_buffer_ref = dxgi_swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D))
		ComPtr<ID3D11Texture2D> back_buffer_ref;
		dxgi_swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*) &back_buffer_ref);

		// back_buffer = d3d11_device->CreateRenderTargetView(back_buffer_ref.Get(), NULL)
		d3d11_device->CreateRenderTargetView(back_buffer_ref.Get(), NULL, &back_buffer);
		d3d11_device_ctx->OMSetRenderTargets(1, &back_buffer, NULL);


		D3D11_VIEWPORT viewport;
		ZeroMemory(&viewport, sizeof(D3D11_VIEWPORT));
		viewport.TopLeftX = 0;
		viewport.TopLeftY = 0;
		viewport.Width = 1920;
		viewport.Height = 1080;
		d3d11_device_ctx->RSSetViewports(1, &viewport);
	}

	void render() {
		float background_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
		d3d11_device_ctx->ClearRenderTargetView(back_buffer.Get(), background_color);

		dxgi_swap_chain->Present(0, 0);
	}
};
*/

int32_t video_main(const char* filename) {
	cv::VideoCapture video = cv::VideoCapture(filename);
	if (!video.isOpened()) {
		std::cerr << "The provided video file/URL was not valid." << std::endl;
		return 1;
	}

	double delay_between_frames = 1000.0 / video.get(cv::CAP_PROP_FPS);

	DWORD w, h, refresh;
	retrieve_desktop_settings(&w, &h, &refresh);

	HWND handle;
	get_wallpaper_handle(&handle);
	VideoRenderer renderer(handle, w, h);

	cv::Mat frame;
	video >> frame;
	for (Timer t; !frame.empty(); video >> frame) {
		t.start();
		renderer.render(frame);
		t.stop();

		double render_time = t.elapsed_ms();
		t.wait(delay_between_frames - render_time);
	}

	return 0;
}

int32_t unity_main(const char* filename) {
	HWND handle;
	get_wallpaper_handle(&handle);

	DWORD w, h, refresh;
	retrieve_desktop_settings(&w, &h, &refresh);
	
	std::stringstream ss_command;
	ss_command << "\"" << filename << "\" -screen-fullscreen 0 -screen-width " << w << " -screen-height " << h << " -nolog -parentHWND " << handle << " delayed";

	std::cout << ss_command.str() << std::endl;
	return 0;

	return system(ss_command.str().c_str());
}

int32_t main(int32_t argc, const const char** argv) {
	if (argc < 2) {
		std::cerr << "An argument must be passed." << std::endl;
		return 1;
	}

	std::string file_str(argv[1]);
	if (file_str.substr(file_str.size() - 4) == ".exe")
		return unity_main(argv[1]);
	else
		return video_main(argv[1]);

	return 0;
}