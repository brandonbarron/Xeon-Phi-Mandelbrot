#include<iostream>
#include<vector>
#include<fstream>
#include<math.h>
#include<chrono>
#include<functional>
#include<numeric>
#include<algorithm>
#include<omp.h>
#include "offload.h"

__attribute__((target(mic))) static constexpr int MAX_ITER = 256;
__attribute__((target(mic))) static constexpr int HEIGHT = 10000;
__attribute__((target(mic))) static constexpr int WIDTH = 10000;
__attribute__((target(mic))) static constexpr double X_MIN = -2.25;
__attribute__((target(mic))) static constexpr double X_MAX = 0.75;
__attribute__((target(mic))) static constexpr double Y_MIN = -1.5;
__attribute__((target(mic))) static constexpr double Y_MAX = 1.5;
__attribute__((target(mic))) static constexpr double X_RANGE = X_MAX - X_MIN;
__attribute__((target(mic))) static constexpr double Y_RANGE = Y_MAX - Y_MIN;


//void __attribute__((target(mic))) Color;
//void __attribute__((target(mic))) Color calcColors(int iteration, double x, double y);

//__attribute__((target(mic)))
struct Color {
	int Red;
	int Green;
	int Blue;

	__attribute__((target(mic)))
	Color() {
		Red = Green = Blue = 0;
	}

	__attribute__((target(mic)))
	Color(int r, int g, int b) {
		Red = r;
		Green = g;
		Blue = b;
	}
};

__attribute__((target(mic))) Color image[WIDTH * HEIGHT];

__attribute__((target(mic)))
int linearIterpolate(int a, int b, double c) {
	auto result = (a * (1.0 - c)) + (b * c);
	return static_cast<int>(std::round(result));
}

__attribute__((target(mic)))
Color linearIterpolateColors(Color color1, Color color2, double c) {
	auto r = linearIterpolate(color1.Red, color2.Red, c);
	auto b = linearIterpolate(color1.Blue, color2.Blue, c);
	auto g = linearIterpolate(color1.Green, color2.Green, c);
	return Color(r, g, b);
}

//the hacky way to get the wikipedia look
//http://stackoverflow.com/questions/16500656/which-color-gradient-is-used-to-color-mandelbrot-in-wikipedia
__attribute__((target(mic)))
Color calcPalette(int iterations) {
	if (iterations < MAX_ITER && iterations > 0) {
		int i = iterations % 16;
		Color mapping[16];
		mapping[0] = Color(66, 30, 15);
		mapping[1] = Color(25, 7, 26);
		mapping[2] = Color(9, 1, 47);
		mapping[3] = Color(4, 4, 73);
		mapping[4] = Color(0, 7, 100);
		mapping[5] = Color(12, 44, 138);
		mapping[6] = Color(24, 82, 177);
		mapping[7] = Color(57, 125, 209);
		mapping[8] = Color(134, 181, 229);
		mapping[9] = Color(211, 236, 248);
		mapping[10] = Color(241, 233, 191);
		mapping[11] = Color(248, 201, 95);
		mapping[12] = Color(255, 170, 0);
		mapping[13] = Color(204, 128, 0);
		mapping[14] = Color(153, 87, 0);
		mapping[15] = Color(106, 52, 3);
		return mapping[i];
	}
	return Color();
}

//adapted from smooth coloring section: https://en.wikipedia.org/wiki/Mandelbrot_set
__attribute__((target(mic)))
Color calcColors(int iteration, double x, double y) {
	auto doubleIter = static_cast<double>(iteration);

	if (doubleIter < MAX_ITER) {
		auto logZn = log(x*x + y*y) / 2;
		auto nu = log(logZn / log(2)) / log(2);
		doubleIter += 1 - nu;
	}
	auto color1 = calcPalette(floor(doubleIter));
	auto color2 = calcPalette(floor(doubleIter) + 1);
	double intpart;
	auto fracPart = modf(doubleIter, &intpart);
	return linearIterpolateColors(color1, color2, fracPart);
}

//adapted from http://codereview.stackexchange.com/questions/124358/mandelbrot-image-generator-and-viewer
__attribute__((target(mic)))
Color calcMandelBrot(double sReal, double sImaginary) {
	auto real = sReal;
	auto imaginary = sImaginary;

	for (auto i = 0; i < MAX_ITER; i++) {
		auto real2 = real * real;
		auto imag2 = imaginary * imaginary;
		if (real2 + imag2 > 16.0) {
			return calcColors(i, real, imaginary);
		}
		imaginary = 2.0 * real * imaginary + sImaginary;
		real = real2 - imag2 + sReal;
	}
	return calcColors(MAX_ITER, real, imaginary);
}

//adapted from https://github.com/sessamekesh/IndigoCS_Mandelbrot/blob/master/main.cpp
__attribute__((target(mic)))
double calcReal(int x) {
	return x * (X_RANGE / WIDTH) + X_MIN;
}

__attribute__((target(mic)))
double calcImaginary(int y) {
	return y * (Y_RANGE / HEIGHT) + Y_MIN;
}

__attribute__((target(mic)))
Color calcPixel(int px, int py) {
	auto real = calcReal(px);
	auto imaginary = calcImaginary(py);
	return calcMandelBrot(real, imaginary);
}

void calcImage() {
#pragma offload target (mic) default(shared)
#pragma omp parallel for
	for (auto x = 0; x < WIDTH; ++x) {
		for (auto y = 0; y < HEIGHT; ++y) {
			image[x * HEIGHT + y] = (calcPixel(x, y));
		}
	}
}

void writeImage() {
	std::ofstream file;
	file.open("output.ppm", std::ios::out | std::ios::binary);
	if (file.is_open()) {
		file << "P6" << std::endl;
		file << WIDTH << " "  << HEIGHT << std::endl;
		file << 255 << std::endl;
		for (auto x = 0; x < WIDTH; x++) {
			for (auto y = 0; y < HEIGHT; y++) {
				auto col = image[y * HEIGHT + x];//re-orient the image for the "look"
				//file.write("%c%c%c" % (col.Red,col.Green,col.Blue));
				file.write((char*)&col.Red, sizeof(char));
				file.write((char*)&col.Green, sizeof(char));
				file.write((char*)&col.Blue, sizeof(char));
			}
			//file << std::endl;
		}
		file.close();
	}
}

//found template struct and execute function here http://codereview.stackexchange.com/questions/48872/measuring-execution-time-in-c
// added much to it
template<typename TimeT = std::chrono::milliseconds>
struct TimeFunctionExecution {
	template<typename F>
	static TimeT executeArgs(F func) {
		auto start = std::chrono::steady_clock::now();

		func();
		auto duration = std::chrono::duration_cast<TimeT>(std::chrono::steady_clock::now() - start);

		return duration;
	}

	template<typename T>
	struct TimeInfo {
		T StandardDeviation;
		T Average;

		TimeInfo(T avg, T stdDev) {
			StandardDeviation = stdDev;
			Average = avg;
		}
	};

	static double calcAverage(std::vector<TimeT> &times) {
		double sum = 0.0;
		std::for_each(times.begin(), times.end(), [&](const TimeT d) {
			auto dDouble = std::chrono::duration <double>(d);
			sum = sum + dDouble.count();
		});
		return sum / times.size();
	}

	static double calcDeviation(std::vector<TimeT> &times, double avg) {
		double accum = 0.0;

		std::for_each(std::begin(times), std::end(times), [&](const TimeT d) {
			auto dfloat = std::chrono::duration < double>(d);
			accum += (dfloat.count() - avg) * (dfloat.count() - avg);
		});

		return sqrt(accum / (times.size() - 1));
	}

	template<typename F>
	static TimeInfo<double> TimeFunction(int iterations, F func) {
		std::vector<TimeT> resultTimes;

		for (auto i = 0; i < iterations; i++) {
			auto result = executeArgs(func);

			resultTimes.push_back(result);
		}
		auto average = calcAverage(resultTimes);
		auto stdDev = calcDeviation(resultTimes, average);
		return TimeInfo<double>(average, stdDev);
	}
};

int main() {
	std::cout << "Starting Computation..." << std::endl;

	//calcImage();
	TimeFunctionExecution<> timer;
	auto timeResult = timer.TimeFunction(1, &calcImage);
	std::cout << "Computation Complete!" << std::endl;
	std::cout << "\tAverage: " << timeResult.Average << " seconds" << std::endl;;
	std::cout << "\tSandard Deviation: " << timeResult.StandardDeviation << " seconds" << std::endl;
	std::cout << "Writting Image..." << std::endl;
	writeImage();
	std::cout << "Done!" << std::endl;
}
