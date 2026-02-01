/* FrameDecoder.cpp
*
*  MIT License
*
*  Copyright (c) 2020-2025 awawa-dev
*
*  Project homesite: https://github.com/awawa-dev/HyperHDR
*
*  Permission is hereby granted, free of charge, to any person obtaining a copy
*  of this software and associated documentation files (the "Software"), to deal
*  in the Software without restriction, including without limitation the rights
*  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
*  copies of the Software, and to permit persons to whom the Software is
*  furnished to do so, subject to the following conditions:
*
*  The above copyright notice and this permission notice shall be included in all
*  copies or substantial portions of the Software.

*  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
*  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
*  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
*  SOFTWARE.
*/


#include <utils/FrameDecoder.h>
#include <utils/VectorizedDecoders.h>
#include <utils/FrameDecoderUtils.h>
#include <base/AutomaticToneMapping.h>
#include <utils/Logger.h>

#include <atomic>
#include <mutex>
#include <numbers>

template<bool Quarter, bool UseToneMapping, bool UseAutomaticToneMapping>
void FrameDecoder::processImageVector(
	int _cropLeft, int _cropRight, int _cropTop, int _cropBottom,
	const uint8_t* data, const uint8_t* dataUV, int width, int height, int lineLength,
	const PixelFormat pixelFormat, const uint8_t* lutBuffer,
	Image<ColorRgb>& outputImage, AutomaticToneMapping* automaticToneMapping)
{
	LoggerName logger("FrameDecoder");

	// validate format
	if (pixelFormat != PixelFormat::YUYV && pixelFormat != PixelFormat::UYVY &&
		pixelFormat != PixelFormat::XRGB && pixelFormat != PixelFormat::RGB24 &&
		pixelFormat != PixelFormat::I420 && pixelFormat != PixelFormat::NV12 &&
		pixelFormat != PixelFormat::P010 && pixelFormat != PixelFormat::MJPEG)
	{
		Error(logger, "Invalid pixel format given");
		return;
	}

	// validate LUT
	if ((pixelFormat == PixelFormat::YUYV || pixelFormat == PixelFormat::UYVY || pixelFormat == PixelFormat::I420 || pixelFormat == PixelFormat::MJPEG ||
		pixelFormat == PixelFormat::NV12 || pixelFormat == PixelFormat::P010 || UseToneMapping) && lutBuffer == nullptr)
	{
		Error(logger, "Missing LUT table for YUV colorspace or tone mapping");
		return;
	}

	// align crop
	_cropLeft = (_cropLeft >> 1) << 1;
	_cropRight = (_cropRight >> 1) << 1;

	if constexpr (Quarter)
		_cropLeft = _cropRight = _cropTop = _cropBottom = 0;

	// output size
	const int outputWidth = (width - _cropLeft - _cropRight) >> (Quarter ? 1 : 0);
	const int outputHeight = (height - _cropTop - _cropBottom) >> (Quarter ? 1 : 0);

	outputImage.resize(outputWidth, outputHeight);
	outputImage.setOriginFormat(pixelFormat);

	uint8_t* destMemory = outputImage.rawMem();
	int      destLineSize = outputImage.width() * 3;

	// ---- P010 ----
	if (pixelFormat == PixelFormat::P010)
	{
		const FrameDecoderUtils& instance = FrameDecoderUtils::instance();
		const auto& lutP010_y = instance.getLutP010_y();
		const auto& lutP010_uv = instance.getlutP010_uv();

		uint8_t* deltaUV = (dataUV != nullptr) ? (uint8_t*)dataUV : (uint8_t*)data + lineLength * height;

		for (int yDest = 0, ySource = _cropTop; yDest < outputHeight; ySource += (Quarter ? 2 : 1), ++yDest)
		{
			uint8_t* currentDest = destMemory + (uint64_t)destLineSize * yDest;
			uint8_t* endDest = currentDest + destLineSize;

			uint8_t* currentSource = (uint8_t*)data + (uint64_t)lineLength * ySource + (uint64_t)_cropLeft;
			uint8_t* currentSourceUV = deltaUV + ((uint64_t)ySource / 2) * lineLength + (uint64_t)_cropLeft;

			if constexpr (UseAutomaticToneMapping) if (yDest % 4 == 0)
			{
				automaticToneMapping->scan_Y_UV_16(width, currentSource, currentSourceUV);
			};

			if constexpr (!UseToneMapping)
			{
				VECTOR_P010::process<Quarter>(
					reinterpret_cast<uint32_t*>(currentSource),
					reinterpret_cast<uint32_t*>(currentSourceUV),
					lutBuffer, currentDest, endDest);
			}
			else
			{
				VECTOR_P010::processlWithToneMapping<Quarter>(
					reinterpret_cast<uint64_t*>(currentSource),
					reinterpret_cast<uint64_t*>(currentSourceUV),
					lutBuffer, currentDest, endDest, lutP010_y, lutP010_uv);
			}
		}

		if constexpr (UseAutomaticToneMapping)
		{
			automaticToneMapping->finilize();
		}
		return;
	}

	// ---- I420 ----
	if (pixelFormat == PixelFormat::I420)
	{
		int deltaU = lineLength * height;
		int deltaV = lineLength * height * 5 / 4;
		for (int yDest = 0, ySource = _cropTop; yDest < outputHeight; ySource += (Quarter ? 2 : 1), ++yDest)
		{
			uint8_t* currentDest = destMemory + ((uint64_t)destLineSize) * yDest;
			uint8_t* endDest = currentDest + destLineSize;
			uint8_t* currentSource = (uint8_t*)data + (((uint64_t)lineLength * ySource) + ((uint64_t)_cropLeft));
			uint8_t* currentSourceU = (uint8_t*)data + deltaU + ((((uint64_t)ySource / 2) * lineLength) + ((uint64_t)_cropLeft)) / 2;
			uint8_t* currentSourceV = (uint8_t*)data + deltaV + ((((uint64_t)ySource / 2) * lineLength) + ((uint64_t)_cropLeft)) / 2;
		
			VECTOR_I420::process<Quarter>(
				reinterpret_cast<uint32_t*>(currentSource),
				reinterpret_cast<uint16_t*>(currentSourceU),
				reinterpret_cast<uint16_t*>(currentSourceV),
				lutBuffer, currentDest, endDest);
		}
		return;
	}

	// ---- NV12 ----
	if (pixelFormat == PixelFormat::NV12)
	{
		uint8_t* deltaUV = (dataUV != nullptr) ? (uint8_t*)dataUV : (uint8_t*)data + lineLength * height;

		for (int yDest = 0, ySource = _cropTop; yDest < outputHeight; ySource += (Quarter ? 2 : 1), ++yDest)
		{
			uint8_t* currentDest = destMemory + (uint64_t)destLineSize * yDest;
			uint8_t* endDest = currentDest + destLineSize;

			uint8_t* currentSource = (uint8_t*)data + (uint64_t)lineLength * ySource + (uint64_t)_cropLeft;
			uint8_t* currentSourceUV = deltaUV + ((uint64_t)ySource / 2) * lineLength + (uint64_t)_cropLeft;

			if constexpr (UseAutomaticToneMapping) if (yDest % 4 == 0)
			{
				automaticToneMapping->scan_Y_UV_8(width, currentSource, currentSourceUV);
			};

			VECTOR_NV12::process<Quarter>(
				reinterpret_cast<uint32_t*>(currentSource),
				reinterpret_cast<uint32_t*>(currentSourceUV),
				lutBuffer, currentDest, endDest);
		}

		if constexpr (UseAutomaticToneMapping)
		{
			automaticToneMapping->finilize();
		}

		return;
	}

	// ---- YUYV ---
	if (pixelFormat == PixelFormat::YUYV)
	{
		for (int yDest = 0, ySource = _cropTop; yDest < outputHeight; ySource += (Quarter ? 2 : 1), ++yDest)
		{
			uint8_t* currentDest = destMemory + ((uint64_t)destLineSize) * yDest;
			uint8_t* endDest = currentDest + destLineSize;
			uint8_t* currentSource = (uint8_t*)data + (((uint64_t)lineLength * ySource) + (((uint64_t)_cropLeft) << 1));

			if constexpr (UseAutomaticToneMapping) if (yDest % 4 == 0)
			{
				automaticToneMapping->scan_YUYV(width, currentSource);
			};

			VECTOR_YUYV::process<Quarter>(
				reinterpret_cast<uint32_t*>(currentSource),
				lutBuffer, currentDest, endDest);
		}

		if constexpr (UseAutomaticToneMapping)
		{
			automaticToneMapping->finilize();
		}

		return;
	}

	// ---- UYVY ---
	if (pixelFormat == PixelFormat::UYVY)
	{
		for (int yDest = 0, ySource = _cropTop; yDest < outputHeight; ySource += (Quarter ? 2 : 1), ++yDest)
		{
			uint8_t* currentDest = destMemory + ((uint64_t)destLineSize) * yDest;
			uint8_t* endDest = currentDest + destLineSize;
			uint8_t* currentSource = (uint8_t*)data + (((uint64_t)lineLength * ySource) + (((uint64_t)_cropLeft) << 1));

			VECTOR_UYVY::process<Quarter>(
				reinterpret_cast<uint32_t*>(currentSource),
				lutBuffer, currentDest, endDest);
		}

		return;
	}

	// ---- RGB24 |  XRGB ---
	if (pixelFormat == PixelFormat::RGB24 || pixelFormat == PixelFormat::XRGB)
	{
		const int bytesPerPixel = (pixelFormat == PixelFormat::RGB24) ? 3 : 4;
		for (int yDest = 0, ySource = height - _cropBottom - 1; yDest < outputHeight; ySource -= (Quarter ? 2 : 1), ++yDest)
		{
			uint8_t* currentDest = destMemory + ((uint64_t)destLineSize) * yDest;
			uint8_t* endDest = currentDest + destLineSize;
			uint8_t* currentSource = (uint8_t*)data + (((uint64_t)lineLength * ySource) + (((uint64_t)_cropLeft) * bytesPerPixel));

			if (pixelFormat == PixelFormat::RGB24)
			{
				VECTOR_RGB::decode<true, Quarter, UseToneMapping>(
					currentSource, lutBuffer, currentDest, endDest);
			}
			else
			{
				VECTOR_RGB::decode<false, Quarter, UseToneMapping>(
					currentSource, lutBuffer, currentDest, endDest);
			}
		}

		return;
	}

	// ---- PixelFormat::MJPEG ---
	if (pixelFormat == PixelFormat::MJPEG)
	{
		int deltaU = lineLength * height;
		int deltaV = lineLength * height * 6 / 4;
		for (int yDest = 0, ySource = _cropTop; yDest < outputHeight; ++ySource, ++yDest)
		{
			uint8_t* currentDest = destMemory + ((uint64_t)destLineSize) * yDest;
			uint8_t* endDest = currentDest + destLineSize;
			uint8_t* currentSource = (uint8_t*)data + (((uint64_t)lineLength * ySource) + ((uint64_t)_cropLeft));
			uint8_t* currentSourceU = (uint8_t*)data + deltaU + ((((uint64_t)ySource) * lineLength) + ((uint64_t)_cropLeft)) / 2;
			uint8_t* currentSourceV = (uint8_t*)data + deltaV + ((((uint64_t)ySource) * lineLength) + ((uint64_t)_cropLeft)) / 2;

			VECTOR_I420::process<Quarter>(
				reinterpret_cast<uint32_t*>(currentSource),
				reinterpret_cast<uint16_t*>(currentSourceU),
				reinterpret_cast<uint16_t*>(currentSourceV),
				lutBuffer, currentDest, endDest);
		}
		return;
	}
}

// Explicitly instantiate the template specializations that are used in GrabberWorker.
template void FrameDecoder::processImageVector<false, false, false>(
	int, int, int, int, const uint8_t*, const uint8_t*, int, int, int, PixelFormat, const uint8_t*, Image<ColorRgb>&, AutomaticToneMapping*);

template void FrameDecoder::processImageVector<false, true, false>(
	int, int, int, int, const uint8_t*, const uint8_t*, int, int, int, PixelFormat, const uint8_t*, Image<ColorRgb>&, AutomaticToneMapping*);

template void FrameDecoder::processImageVector<true, false, false>(
	int, int, int, int, const uint8_t*, const uint8_t*, int, int, int, PixelFormat, const uint8_t*, Image<ColorRgb>&, AutomaticToneMapping*);

template void FrameDecoder::processImageVector<true, true, false>(
	int, int, int, int, const uint8_t*, const uint8_t*, int, int, int, PixelFormat, const uint8_t*, Image<ColorRgb>&, AutomaticToneMapping*);

template void FrameDecoder::processImageVector<false, false, true>(
	int, int, int, int, const uint8_t*, const uint8_t*, int, int, int, PixelFormat, const uint8_t*, Image<ColorRgb>&, AutomaticToneMapping*);

template void FrameDecoder::processImageVector<false, true, true>(
	int, int, int, int, const uint8_t*, const uint8_t*, int, int, int, PixelFormat, const uint8_t*, Image<ColorRgb>&, AutomaticToneMapping*);

template void FrameDecoder::processImageVector<true, false, true>(
	int, int, int, int, const uint8_t*, const uint8_t*, int, int, int, PixelFormat, const uint8_t*, Image<ColorRgb>&, AutomaticToneMapping*);

template void FrameDecoder::processImageVector<true, true, true>(
	int, int, int, int, const uint8_t*, const uint8_t*, int, int, int, PixelFormat, const uint8_t*, Image<ColorRgb>&, AutomaticToneMapping*);

void FrameDecoder::applyLUT(uint8_t* _source, unsigned int width, unsigned int height, const uint8_t* lutBuffer, const int _hdrToneMappingEnabled)
{
	uint8_t buffer[8];

	if (lutBuffer != nullptr && _hdrToneMappingEnabled)
	{
		unsigned int sizeX = (width * 10) / 100;
		unsigned int sizeY = (height * 25) / 100;

		for (unsigned int y = 0; y < height; y++)
		{
			unsigned char* startSource = _source + static_cast<size_t>(width) * 3 * y;
			unsigned char* endSource = startSource + static_cast<size_t>(width) * 3;

			if (_hdrToneMappingEnabled != 2 || y < sizeY || y > height - sizeY)
			{
				while (startSource < endSource)
				{
					*((uint32_t*)&buffer) = *((uint32_t*)startSource);					
					uint32_t ind_lutd = LUT_INDEX(buffer[0], buffer[1], buffer[2]);
					memcpy(startSource, &(lutBuffer[ind_lutd]), 3);
					startSource += 3;
				}
			}
			else
			{
				unsigned int x = 0;
				while (startSource < endSource)
				{
					if (x++ == sizeX)
						startSource += (width - 2 * static_cast<size_t>(sizeX)) * 3;

					*((uint32_t*)&buffer) = *((uint32_t*)startSource);
					uint32_t ind_lutd = LUT_INDEX(buffer[0], buffer[1], buffer[2]);
					memcpy(startSource, &(lutBuffer[ind_lutd]), 3);
					startSource += 3;
				}
			}
		}
	}
}

void FrameDecoder::processSystemImageBGRA(Image<ColorRgb>& image, int targetSizeX, int targetSizeY,
	int startX, int startY,
	uint8_t* source, int _actualWidth, int _actualHeight,
	int division, uint8_t* _lutBuffer, int lineSize)
{
	uint32_t	ind_lutd;
	uint8_t		buffer[8];
	size_t		divisionX = (size_t)division * 4;

	if (lineSize == 0)
		lineSize = _actualWidth * 4;

	for (int j = 0; j < targetSizeY; j++)
	{
		size_t lineSource = std::min(startY + j * division, _actualHeight - 1);
		uint8_t* dLine = (image.rawMem() + (size_t)j * targetSizeX * 3);
		uint8_t* dLineEnd = dLine + (size_t)targetSizeX * 3;
		uint8_t* sLine = ((source + (lineSource * lineSize) + ((size_t)startX * 4)));

		if (_lutBuffer == nullptr)
		{
			sLine += 2;
			while (dLine < dLineEnd)
			{
				*dLine++ = *sLine--;
				*dLine++ = *sLine--;
				*dLine++ = *sLine;
				sLine += divisionX + 2;
			}
		}
		else while (dLine < dLineEnd)
		{
			*((uint32_t*)&buffer) = *((uint32_t*)sLine);
			sLine += divisionX;
			ind_lutd = LUT_INDEX(buffer[2], buffer[1], buffer[0]);
			*((uint32_t*)dLine) = *((uint32_t*)(&_lutBuffer[ind_lutd]));
			dLine += 3;
		}
	}
}

/*void FrameDecoder::processSystemImageBGR(Image<ColorRgb>& image, int targetSizeX, int targetSizeY,
	int startX, int startY,
	uint8_t* source, int _actualWidth, int _actualHeight,
	int division, uint8_t* _lutBuffer, int lineSize)
{
	uint32_t	ind_lutd;
	uint8_t		buffer[8];
	size_t		divisionX = (size_t)division * 3;

	if (lineSize == 0)
		lineSize = _actualWidth * 3;

	for (int j = 0; j < targetSizeY; j++)
	{
		size_t lineSource = std::min(startY + j * division, _actualHeight - 1);
		uint8_t* dLine = (image.rawMem() + (size_t)j * targetSizeX * 3);
		uint8_t* dLineEnd = dLine + (size_t)targetSizeX * 3;
		uint8_t* sLine = ((source + (lineSource * lineSize) + ((size_t)startX * 3)));

		if (_lutBuffer == nullptr)
		{
			sLine += 2;
			while (dLine < dLineEnd)
			{
				*dLine++ = *sLine--;
				*dLine++ = *sLine--;
				*dLine++ = *sLine;
				sLine += divisionX + 2;
			}
		}
		else while (dLine < dLineEnd)
		{	
			memcpy(&buffer, &sLine, 3);
			sLine += divisionX;
			ind_lutd = LUT_INDEX(buffer[2], buffer[1], buffer[0]);
			*((uint32_t*)dLine) = *((uint32_t*)(&_lutBuffer[ind_lutd]));
			dLine += 3;
		}
	}
}*/

//Esta es la buena
/*void FrameDecoder::processSystemImageBGR(Image<ColorRgb>& image, int targetSizeX, int targetSizeY,
	int startX, int startY,
	uint8_t* source, int _actualWidth, int _actualHeight,
	int division, uint8_t* _lutBuffer, int lineSize)
{
	uint32_t ind_lutd;
	uint8_t buffer[8];
	size_t divisionX = (size_t)division * 3;

	if (lineSize == 0)
		lineSize = _actualWidth * 3;

	for (int j = 0; j < targetSizeY; j++)
	{
		size_t lineSource = std::min(startY + j * division, _actualHeight - 1);
		uint8_t* dLine = image.rawMem() + (size_t)j * targetSizeX * 3;
		uint8_t* dLineEnd = dLine + (size_t)targetSizeX * 3;
		uint8_t* sLine = ((source + (lineSource * lineSize) + ((size_t)startX * 3)));

		// Recorremos la línea
		sLine += 2; // Ajuste inicial para invertir BGR→RGB
		while (dLine < dLineEnd)
		{
			// Convertimos BGR -> RGB en buffer
			buffer[0] = *sLine--; // R
			buffer[1] = *sLine--; // G
			buffer[2] = *sLine;   // B
			sLine += divisionX + 2;

			// Aplicar LUT si existe
			if (_lutBuffer != nullptr)
			{
				ind_lutd = LUT_INDEX(buffer[0], buffer[1], buffer[2]);
				memcpy(buffer, &_lutBuffer[ind_lutd], 3);
			}

			// Copiar resultado a la línea de destino
			memcpy(dLine, buffer, 3);
			dLine += 3;
		}
	}
}
*/
//Con esta sale azul
/*void FrameDecoder::processSystemImageBGR(Image<ColorRgb>& image, int targetSizeX, int targetSizeY,
	int startX, int startY,
	uint8_t* source, int _actualWidth, int _actualHeight,
	int division, uint8_t* _lutBuffer, int lineSize)
{
	uint32_t ind_lutd;
	uint8_t buffer[8];
	size_t divisionX = (size_t)division * 3;

	if (lineSize == 0)
		lineSize = _actualWidth * 3;

	for (int j = 0; j < targetSizeY; j++)
	{
		size_t lineSource = std::min(startY + j * division, _actualHeight - 1);
		uint8_t* dLine = image.rawMem() + (size_t)j * targetSizeX * 3;
		uint8_t* dLineEnd = dLine + (size_t)targetSizeX * 3;
		uint8_t* sLine = source + (lineSource * lineSize) + ((size_t)startX * 3);

		// Ajuste inicial para invertir BGR → RGB
		sLine += 2;

		while (dLine < dLineEnd)
		{
			// BGR → RGB
			buffer[0] = *sLine--; // R
			buffer[1] = *sLine--; // G
			buffer[2] = *sLine;   // B
			sLine += divisionX + 2;

			if (_lutBuffer != nullptr)
			{
				// RGB → YUV (BT.709 limited)
				int Yaux = static_cast<int>(16.0  + (0.1826 * buffer[0] + 0.6142 * buffer[1] + 0.0620 * buffer[2]));
				int Uaux = static_cast<int>(128.0 + (-0.1006 * buffer[0] - 0.3386 * buffer[1] + 0.4392 * buffer[2]));
				int Vaux = static_cast<int>(128.0 + (0.4392 * buffer[0] - 0.3989 * buffer[1] - 0.0403 * buffer[2]));

				uint8_t Y = static_cast<uint8_t>(std::clamp(Yaux, 16, 235));
				uint8_t U = static_cast<uint8_t>(std::clamp(Uaux, 16, 240));
				uint8_t V = static_cast<uint8_t>(std::clamp(Vaux, 16, 240));

				// LUT YUV → YUV (calibrador)
				ind_lutd = LUT_INDEX(Y, U, V);
				memcpy(buffer, &_lutBuffer[ind_lutd], 3);

				// --- YUV → RGB (BT.709 limited) ---
				uint8_t Yc = buffer[0];
				uint8_t Uc = buffer[1];
				uint8_t Vc = buffer[2];

				int C = (int)Yc - 16;
				int D = (int)Uc - 128;
				int E = (int)Vc - 128;

				int R = (298 * C + 459 * E + 128) >> 8;
				int G = (298 * C -  55 * D - 136 * E + 128) >> 8;
				int B = (298 * C + 541 * D + 128) >> 8;

				buffer[0] = static_cast<uint8_t>(std::clamp(R, 0, 255));
				buffer[1] = static_cast<uint8_t>(std::clamp(G, 0, 255));
				buffer[2] = static_cast<uint8_t>(std::clamp(B, 0, 255));
			}

			// Escribir RGB final
			memcpy(dLine, buffer, 3);
			dLine += 3;
		}
	}
}*/

void FrameDecoder::processSystemImageBGR(Image<ColorRgb>& image, int targetSizeX, int targetSizeY,
    int startX, int startY,
    uint8_t* source, int _actualWidth, int _actualHeight,
    int division, uint8_t* _lutBuffer, int lineSize)
{
    uint32_t ind_lutd;
    uint8_t buffer[3];
    size_t divisionX = (size_t)division * 3;
	LoggerName logger("FrameDecoder");

	static bool logOnce = true;
    if (logOnce)
    {
        Info(logger, "processSystemImageBGR called: _lutBuffer={}", 
             (_lutBuffer != nullptr) ? "NOT NULL" : "NULL");
        logOnce = false;
    }

    if (lineSize == 0)
        lineSize = _actualWidth * 3;

    for (int j = 0; j < targetSizeY; j++)
    {
        size_t lineSource = std::min(startY + j * division, _actualHeight - 1);
        uint8_t* dLine = image.rawMem() + (size_t)j * targetSizeX * 3;
        uint8_t* dLineEnd = dLine + (size_t)targetSizeX * 3;
        uint8_t* sLine = source + (lineSource * lineSize) + ((size_t)startX * 3);

        // Ajuste inicial para invertir BGR → RGB
        sLine += 2;

        while (dLine < dLineEnd)
        {
            // Leer BGR y convertir a RGB
            uint8_t R = *sLine--;
            uint8_t G = *sLine--;
            uint8_t B = *sLine;
            sLine += divisionX + 2;

            if (_lutBuffer != nullptr)
            {
				static bool loggedFirst = false;
				if (!loggedFirst){
					Info(logger, "Framedecoder.cpp processSystemImageBGR: Aplica LUT");
					loggedFirst = true;
				}
                // --- INDEXAR LUT con RGB originales ---
                ind_lutd = LUT_INDEX(R, G, B);
                uint8_t Y_lut = _lutBuffer[ind_lutd + 0];
                uint8_t U_lut = _lutBuffer[ind_lutd + 1];
                uint8_t V_lut = _lutBuffer[ind_lutd + 2];

                // --- CONVERTIR YUV → RGB BT.709 limitada ---
                int C = (int)Y_lut - 16;
                int D = (int)U_lut - 128;
                int E = (int)V_lut - 128;

                int Rf = std::clamp(int(1.164f * C + 1.793f * E), 0, 255);
                int Gf = std::clamp(int(1.164f * C - 0.213f * D - 0.533f * E), 0, 255);
                int Bf = std::clamp(int(1.164f * C + 2.112f * D), 0, 255);

				static int debugCount = 0;
				if (debugCount < 10) {
					Info(logger,
						"LUT APPLY PIXEL idx={} | "
						"RGB_in=[{},{},{}] | "
						"YUV_lut=[{},{},{}] | "
						"C={} D={} E={} | "
						"RGB_out=[{},{},{}]",
						ind_lutd,
						R, G, B,
						Y_lut, U_lut, V_lut,
						C, D, E,
						Rf, Gf, Bf
					);
					debugCount++;
				}

                buffer[0] = static_cast<uint8_t>(Rf);
                buffer[1] = static_cast<uint8_t>(Gf);
                buffer[2] = static_cast<uint8_t>(Bf);
            }
            else
            {
                // Sin LUT, solo RGB directo
                buffer[0] = R;
                buffer[1] = G;
                buffer[2] = B;
				static bool loggedNFirst = false;
				if (!loggedNFirst){
					Info(logger, "Framedecoder.cpp processSystemImageBGR: NO aplica LUT");
					loggedNFirst = true;
				}
            }

            // Escribir RGB final
            memcpy(dLine, buffer, 3);
            dLine += 3;
        }
    }
}





void FrameDecoder::processSystemImageBGR16(Image<ColorRgb>& image, int targetSizeX, int targetSizeY,
	int startX, int startY,
	uint8_t* source, int _actualWidth, int _actualHeight,
	int division, uint8_t* _lutBuffer, int lineSize)
{
	uint32_t	ind_lutd;
	uint8_t		buffer[8];
	size_t		divisionX = (size_t)division * 2;

	if (lineSize == 0)
		lineSize = _actualWidth * 2;

	for (int j = 0; j < targetSizeY; j++)
	{
		size_t lineSource = std::min(startY + j * division, _actualHeight - 1);
		uint8_t* dLine = (image.rawMem() + (size_t)j * targetSizeX * 3);
		uint8_t* dLineEnd = dLine + (size_t)targetSizeX * 3;
		uint8_t* sLine = ((source + (lineSource * lineSize) + ((size_t)startX * 2)));

		if (_lutBuffer == nullptr)
		{
			sLine += 2;
			while (dLine < dLineEnd)
			{
				*((uint16_t*)&buffer) = *((uint16_t*)sLine);

				*dLine++ = (buffer[1] & 0xF8);
				*dLine++ = (((buffer[1] & 0x7) << 3) | (buffer[0] & 0xE0) >> 5) << 2;
				*dLine++ = (buffer[0] & 0x1f) << 3;

				sLine += divisionX;
			}
		}
		else while (dLine < dLineEnd)
		{
			*((uint16_t*)&buffer) = *((uint16_t*)sLine);
			sLine += divisionX;

			buffer[3] = (buffer[1] & 0xF8);
			buffer[4] = (((buffer[1] & 0x7) << 3) | (buffer[0] & 0xE0) >> 5) << 2;
			buffer[5] = (buffer[0] & 0x1f) << 3;

			ind_lutd = LUT_INDEX(buffer[3], buffer[4], buffer[5]);
			*((uint32_t*)dLine) = *((uint32_t*)(&_lutBuffer[ind_lutd]));
			dLine += 3;
		}
	}
}



void FrameDecoder::processSystemImageRGBA(Image<ColorRgb>& image, int targetSizeX, int targetSizeY,
											int startX, int startY,
											uint8_t* source, int _actualWidth, int _actualHeight,
											int division, uint8_t* _lutBuffer, int lineSize)
{
	uint32_t	ind_lutd;
	uint8_t		buffer[8];
	size_t		divisionX = (size_t)division * 4;

	if (lineSize == 0)
		lineSize = _actualWidth * 4;

	for (int j = 0; j < targetSizeY; j++)
	{
		size_t lineSource = std::min(startY + j * division, _actualHeight - 1);
		uint8_t* dLine = (image.rawMem() + (size_t)j * targetSizeX * 3);
		uint8_t* dLineEnd = dLine + (size_t)targetSizeX * 3;
		uint8_t* sLine = ((source + (lineSource * lineSize) + ((size_t)startX * 4)));

		if (_lutBuffer == nullptr)
		{
			while (dLine < dLineEnd)
			{
				*dLine++ = *sLine++;
				*dLine++ = *sLine++;
				*dLine++ = *sLine;
				sLine += divisionX - 2;
			}
		}
		else while (dLine < dLineEnd)
		{
			*((uint32_t*)&buffer) = *((uint32_t*)sLine);
			sLine += divisionX;
			ind_lutd = LUT_INDEX(buffer[2], buffer[1], buffer[0]);
			*((uint32_t*)dLine) = *((uint32_t*)(&_lutBuffer[ind_lutd]));
			dLine += 3;
		}
	}
}

void FrameDecoder::processSystemImagePQ10(Image<ColorRgb>& image, int targetSizeX, int targetSizeY,
	int startX, int startY,
	uint8_t* source, int _actualWidth, int _actualHeight,
	int division, uint8_t* _lutBuffer, int lineSize)
{
	uint32_t	ind_lutd;
	size_t		divisionX = (size_t)division * 4;

	if (lineSize == 0)
		lineSize = _actualWidth * 4;

	for (int j = 0; j < targetSizeY; j++)
	{
		size_t lineSource = std::min(startY + j * division, _actualHeight - 1);
		uint8_t* dLine = (image.rawMem() + (size_t)j * targetSizeX * 3);
		uint8_t* dLineEnd = dLine + (size_t)targetSizeX * 3;
		uint8_t* sLine = ((source + (lineSource * lineSize) + ((size_t)startX * 4)));

		if (_lutBuffer == nullptr)
		{
			while (dLine < dLineEnd)
			{
				uint32_t inS = *((uint32_t*)sLine);
				*(dLine++) = (inS >> 2) & 0xFF;
				*(dLine++) = (inS >> 12) & 0xFF;
				*(dLine++) = (inS >> 22) & 0xFF;				
				sLine += divisionX;
			}
		}
		else while (dLine < dLineEnd)
		{
			uint32_t inS = *((uint32_t*)sLine);			
			ind_lutd = LUT_INDEX(((inS >> 2) & 0xFF), ((inS >> 12) & 0xFF), ((inS >> 22) & 0xFF));
			*((uint32_t*)dLine) = *((uint32_t*)(&_lutBuffer[ind_lutd]));
			sLine += divisionX;
			dLine += 3;
		}
	}
}


