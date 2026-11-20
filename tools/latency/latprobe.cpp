/*
===========================================================================
Copyright (C) 2026, xenjk contributors

This file is part of xenjk, an OpenJK derivative.

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

// latprobe: mouse-to-desktop latency of a game window, without admin rights.
//
//   latprobe --pid <pid> [--samples 120] [--move 400] [--threshold 10] [--gap-min 150] [--gap-max 350]
//            [--timeout 500] [--csv out.csv]
//
// Each sample injects one relative mouse move with SendInput (the view turns), then watches the desktop with
// DXGI Desktop Duplication until a centre region of the window differs from the frame before the move. The
// latency is the duplication frame's LastPresentTime (when the desktop image that first shows the turn was
// updated) minus the QueryPerformanceCounter time just before SendInput. Moves alternate left/right so the view
// returns; both directions are samples. What it includes: input queueing, the game's frame and its GPU queue,
// presentation and desktop composition. What it leaves out: mouse USB polling and the monitor's scan-out and
// response. It is a relative measure for before/after and renderer comparisons, not an absolute photon time.
// Desktop Duplication itself may keep the game composed (no independent flip) for every renderer alike.
//
// Build: tools/latency/build_latprobe.cmd (cl, C++17, d3d11/dxgi). Output: CSV per sample, summary on stdout.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <timeapi.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "winmm.lib")

template <class T> static void Release(T *&p) { if (p) { p->Release(); p = nullptr; } }

static double g_qpcMs;
static double QpcMs(LONGLONG t) { return (double)t / g_qpcMs; }
static LONGLONG Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }

struct FindCtx { DWORD pid; HWND best; LONG bestArea; };

static BOOL CALLBACK EnumProc(HWND h, LPARAM lp)
{
	FindCtx *c = (FindCtx *)lp;
	DWORD pid = 0;
	GetWindowThreadProcessId(h, &pid);
	if (pid != c->pid || !IsWindowVisible(h) || GetWindow(h, GW_OWNER))
		return TRUE;
	RECT r;
	GetWindowRect(h, &r);
	LONG area = (r.right - r.left) * (r.bottom - r.top);
	if (area > c->bestArea) { c->best = h; c->bestArea = area; }
	return TRUE;
}

static bool BringToFront(HWND h)
{
	for (int attempt = 0; attempt < 20; attempt++) {
		if (GetForegroundWindow() == h)
			return true;
		// an ALT tap lets a background process take the foreground (the foreground lock is lifted by input)
		INPUT alt[2] = {};
		alt[0].type = alt[1].type = INPUT_KEYBOARD;
		alt[0].ki.wVk = alt[1].ki.wVk = VK_MENU;
		alt[1].ki.dwFlags = KEYEVENTF_KEYUP;
		SendInput(2, alt, sizeof(INPUT));
		ShowWindow(h, SW_RESTORE);
		SetForegroundWindow(h);
		Sleep(100);
	}
	return GetForegroundWindow() == h;
}

struct Dup {
	ID3D11Device *dev = nullptr;
	ID3D11DeviceContext *ctx = nullptr;
	IDXGIOutputDuplication *dup = nullptr;
	ID3D11Texture2D *staging = nullptr;
	RECT outRect = {};
	D3D11_BOX box = {};
	UINT w = 0, h = 0;
};

static bool InitDup(Dup &d, HWND win, int regionSize)
{
	HMONITOR mon = MonitorFromWindow(win, MONITOR_DEFAULTTONEAREST);
	IDXGIFactory1 *factory = nullptr;
	if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)&factory)))
		return false;
	IDXGIAdapter1 *adapter = nullptr;
	IDXGIOutput *output = nullptr;
	for (UINT a = 0; !output && factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; a++) {
		IDXGIOutput *o = nullptr;
		for (UINT i = 0; adapter->EnumOutputs(i, &o) != DXGI_ERROR_NOT_FOUND; i++) {
			DXGI_OUTPUT_DESC desc;
			o->GetDesc(&desc);
			if (desc.Monitor == mon) { output = o; d.outRect = desc.DesktopCoordinates; break; }
			Release(o);
		}
		if (!output)
			Release(adapter);
	}
	Release(factory);
	if (!output) { fprintf(stderr, "latprobe: no DXGI output for the window's monitor\n"); return false; }

	D3D_FEATURE_LEVEL fl;
	HRESULT hr = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
		&d.dev, &fl, &d.ctx);
	Release(adapter);
	if (FAILED(hr)) { fprintf(stderr, "latprobe: D3D11CreateDevice 0x%08lx\n", hr); Release(output); return false; }

	IDXGIOutput1 *out1 = nullptr;
	output->QueryInterface(__uuidof(IDXGIOutput1), (void **)&out1);
	Release(output);
	if (!out1) return false;
	hr = out1->DuplicateOutput(d.dev, &d.dup);
	Release(out1);
	if (FAILED(hr)) { fprintf(stderr, "latprobe: DuplicateOutput 0x%08lx\n", hr); return false; }

	// centre region of the window, in output coordinates
	RECT wr;
	GetWindowRect(win, &wr);
	LONG cx = (wr.left + wr.right) / 2 - d.outRect.left, cy = (wr.top + wr.bottom) / 2 - d.outRect.top;
	LONG half = regionSize / 2;
	LONG ow = d.outRect.right - d.outRect.left, oh = d.outRect.bottom - d.outRect.top;
	d.box.left = (UINT)std::max(0L, cx - half);
	d.box.top = (UINT)std::max(0L, cy - half);
	d.box.right = (UINT)std::min(ow, cx + half);
	d.box.bottom = (UINT)std::min(oh, cy + half);
	d.box.front = 0;
	d.box.back = 1;
	d.w = d.box.right - d.box.left;
	d.h = d.box.bottom - d.box.top;

	D3D11_TEXTURE2D_DESC td = {};
	td.Width = d.w;
	td.Height = d.h;
	td.MipLevels = 1;
	td.ArraySize = 1;
	td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	td.SampleDesc.Count = 1;
	td.Usage = D3D11_USAGE_STAGING;
	td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	return SUCCEEDED(d.dev->CreateTexture2D(&td, nullptr, &d.staging));
}

// Acquire the next desktop frame (up to timeoutMs). On success copies the region into pixels and returns the
// frame's LastPresentTime (QPC; 0 when only the pointer moved). Returns -1 on timeout, -2 on a lost duplication.
static LONGLONG NextFrame(Dup &d, UINT timeoutMs, std::vector<unsigned char> &pixels)
{
	DXGI_OUTDUPL_FRAME_INFO info;
	IDXGIResource *res = nullptr;
	HRESULT hr = d.dup->AcquireNextFrame(timeoutMs, &info, &res);
	if (hr == DXGI_ERROR_WAIT_TIMEOUT)
		return -1;
	if (FAILED(hr))
		return -2;
	LONGLONG when = info.LastPresentTime.QuadPart;
	if (when) {
		ID3D11Texture2D *tex = nullptr;
		res->QueryInterface(__uuidof(ID3D11Texture2D), (void **)&tex);
		d.ctx->CopySubresourceRegion(d.staging, 0, 0, 0, 0, tex, 0, &d.box);
		Release(tex);
		D3D11_MAPPED_SUBRESOURCE m;
		if (SUCCEEDED(d.ctx->Map(d.staging, 0, D3D11_MAP_READ, 0, &m))) {
			pixels.resize((size_t)d.w * d.h * 4);
			for (UINT y = 0; y < d.h; y++)
				memcpy(&pixels[(size_t)y * d.w * 4], (unsigned char *)m.pData + (size_t)y * m.RowPitch, (size_t)d.w * 4);
			d.ctx->Unmap(d.staging, 0);
		}
	}
	Release(res);
	d.dup->ReleaseFrame();
	return when;
}

static double MeanAbsDiff(const std::vector<unsigned char> &a, const std::vector<unsigned char> &b)
{
	if (a.size() != b.size() || a.empty())
		return 0.0;
	unsigned long long sum = 0, n = 0;
	for (size_t i = 0; i + 3 < a.size(); i += 16) {   // every 4th pixel, colour channels
		for (int c = 0; c < 3; c++)
			sum += (unsigned)std::abs((int)a[i + c] - (int)b[i + c]);
		n += 3;
	}
	return n ? (double)sum / n : 0.0;
}

static void Move(int dx)
{
	INPUT in = {};
	in.type = INPUT_MOUSE;
	in.mi.dx = dx;
	in.mi.dwFlags = MOUSEEVENTF_MOVE;
	SendInput(1, &in, sizeof(INPUT));
}

static double Percentile(std::vector<double> v, double p)
{
	if (v.empty()) return 0.0;
	std::sort(v.begin(), v.end());
	double k = (v.size() - 1) * p / 100.0;
	size_t f = (size_t)k;
	return f + 1 < v.size() ? v[f] + (v[f + 1] - v[f]) * (k - f) : v[f];
}

int main(int argc, char **argv)
{
	DWORD pid = 0;
	int samples = 120, move = 400, gapMin = 150, gapMax = 350, timeoutMs = 500, region = 256;
	double threshold = 10.0;
	const char *csvPath = nullptr;
	for (int i = 1; i < argc; i++) {
		std::string a = argv[i];
		const char *v = i + 1 < argc ? argv[i + 1] : nullptr;
		if (!v) break;
		if (a == "--pid") pid = (DWORD)atol(v);
		else if (a == "--samples") samples = atoi(v);
		else if (a == "--move") move = atoi(v);
		else if (a == "--threshold") threshold = atof(v);
		else if (a == "--gap-min") gapMin = atoi(v);
		else if (a == "--gap-max") gapMax = atoi(v);
		else if (a == "--timeout") timeoutMs = atoi(v);
		else if (a == "--region") region = atoi(v);
		else if (a == "--csv") csvPath = v;
		else continue;
		i++;
	}
	if (!pid) { fprintf(stderr, "usage: latprobe --pid <pid> [--samples N] [--move counts] [--csv path]\n"); return 2; }

	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	LARGE_INTEGER f;
	QueryPerformanceFrequency(&f);
	g_qpcMs = (double)f.QuadPart / 1000.0;
	timeBeginPeriod(1);

	FindCtx fc = { pid, nullptr, 0 };
	EnumWindows(EnumProc, (LPARAM)&fc);
	if (!fc.best) { fprintf(stderr, "latprobe: no visible window for pid %lu\n", pid); return 3; }
	if (!BringToFront(fc.best)) { fprintf(stderr, "latprobe: could not bring the game window to the front\n"); return 4; }
	Sleep(500);

	Dup d;
	if (!InitDup(d, fc.best, region)) return 5;

	FILE *csv = csvPath ? fopen(csvPath, "w") : nullptr;
	if (csv) fprintf(csv, "sample,direction,latency_ms,diff,result\n");

	std::mt19937 rng(12345);
	std::uniform_int_distribution<int> gap(gapMin, gapMax);
	std::vector<unsigned char> baseline, pixels;
	std::vector<double> lat;
	int misses = 0, lostFocus = 0;

	for (int s = 0; s < samples; s++) {
		if (GetForegroundWindow() != fc.best) {
			lostFocus++;
			if (!BringToFront(fc.best)) break;
		}
		// settle: take every frame for a while; the last one is the baseline the move must change
		LONGLONG until = Now() + (LONGLONG)(gap(rng) * g_qpcMs);
		while (Now() < until) {
			LONGLONG r = NextFrame(d, 5, pixels);
			if (r == -2) { fprintf(stderr, "latprobe: duplication lost\n"); goto done; }
			if (r > 0) baseline = pixels;
		}
		if (baseline.empty()) { misses++; continue; }

		int dir = (s % 2) ? -1 : 1;
		LONGLONG t0 = Now();
		Move(dir * move);
		double latency = -1.0, diff = 0.0;
		LONGLONG deadline = t0 + (LONGLONG)(timeoutMs * g_qpcMs);
		while (Now() < deadline) {
			LONGLONG r = NextFrame(d, 2, pixels);
			if (r == -2) { fprintf(stderr, "latprobe: duplication lost\n"); goto done; }
			if (r <= 0) continue;
			diff = MeanAbsDiff(pixels, baseline);
			if (diff >= threshold) {
				latency = QpcMs(r - t0);
				break;
			}
		}
		if (latency >= 0) lat.push_back(latency);
		else misses++;
		if (csv) fprintf(csv, "%d,%d,%.3f,%.2f,%s\n", s, dir, latency, diff, latency >= 0 ? "ok" : "miss");
	}
done:
	if (csv) fclose(csv);
	printf("latprobe samples=%zu misses=%d lost_focus=%d median_ms=%.2f p10_ms=%.2f p90_ms=%.2f mean_ms=%.2f\n",
		lat.size(), misses, lostFocus, Percentile(lat, 50), Percentile(lat, 10), Percentile(lat, 90),
		lat.empty() ? 0.0 : [&] { double t = 0; for (double v : lat) t += v; return t / lat.size(); }());
	Release(d.staging);
	Release(d.dup);
	Release(d.ctx);
	Release(d.dev);
	timeEndPeriod(1);
	return lat.empty() ? 1 : 0;
}
