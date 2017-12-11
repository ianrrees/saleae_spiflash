/*
MIT License

Copyright(c) 2017 Jerzy Kasenberg

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files(the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
#include "SpiFlashAnalyzerResults.h"
#include <AnalyzerHelpers.h>
#include "SpiFlashAnalyzer.h"
#include "SpiFlashAnalyzerSettings.h"
#include <iostream>
#include <fstream>
#include <sstream>

#include "SpiFlash.h"

SpiFlashAnalyzerResults::SpiFlashAnalyzerResults(SpiFlashAnalyzer* analyzer, SpiFlashAnalyzerSettings* settings)
	: AnalyzerResults(),
	mSettings(settings),
	mAnalyzer(analyzer)
{
}

SpiFlashAnalyzerResults::~SpiFlashAnalyzerResults()
{
}

static int AddressBits(U32 addr)
{
	if (addr < 0x100)
		return 8;
	else if (addr < 0x10000)
		return 16;
	else if (addr < 0x1000000)
		return 24;
	else
		return 32;
}

static std::string RegisterString(RegisterData *reg, U64 val, bool full = false)
{
	std::stringstream s;

	for (size_t i = 0; i < reg->GetBitfieldCount(); ++i)
	{
		const BitField &bitField = reg->at(i);
		U32 bitsValue = bitField.GetValue(val);
		if (bitsValue || full)
			s << (s.tellp() ? " " : "") << bitField.mFieldName << "=" << std::hex << bitsValue;
	}
	return s.str();
}

void SpiFlashAnalyzerResults::AddRegisterResult(RegisterData *reg, U64 val, DisplayBase display_base)
{
	char number_str[128];
	AnalyzerHelpers::GetNumberString(val, display_base, 8, number_str, 128);
	AddResultString(number_str);
	// There is register assigned
	if (reg)
	{
		std::string s = RegisterString(reg, val);
		if (s.size())
			AddResult(s);
		AddResult(RegisterString(reg, val, true));
	}
}

void SpiFlashAnalyzerResults::GenerateBubbleText(U64 frame_index, Channel& channel, DisplayBase display_base)
{
	ClearResultStrings();
	Frame frame = GetFrame(frame_index);

	char number_str[128];
	char number_str2[10];
	std::stringstream fulls, shorts;

	if (frame.mType == FT_CMD && channel == mSettings->mChipSelect)
	{
		SpiCmdData *cmd = reinterpret_cast<SpiCmdData *>(frame.mData2);
		if (U64(cmd) > 0x100)
		{
			size_t i;
			const char *s[4] = { 0 };
			for (i = 0; i < cmd->mNames.size(); ++i)
				AddResultString(cmd->mNames[i].c_str());
			if (cmd->mHasAddr)
			{
				U32 addr = U32(frame.mData1 >> 24);
				s[0] = "  A=";
				s[1] = number_str;
				AnalyzerHelpers::GetNumberString(addr, Hexadecimal, AddressBits(addr), number_str, 128);
			}
			if (cmd->mCmdOp == OP_DATA_READ || cmd->mCmdOp == OP_DATA_WRITE)
			{
				s[2] = "  bytes:";
				s[3] = number_str2;
				AnalyzerHelpers::GetNumberString(frame.mData1 & 0xFFFFFF, Decimal, 24, number_str2, 128);
			}
			// Add longest name with address and byte count if present
			AddResultString(cmd->mNames[i - 1].c_str(), s[0], s[1], s[2], s[3]);
		}
		else
		{
			AddResultString("??");
			if (frame.mData2 != 0x100)
			{
				AnalyzerHelpers::GetNumberString(frame.mData2, display_base, 8, number_str, 128);
				AddResultString("?? CMD=", number_str);
			}
		}
	}
	else if (frame.mType == FT_CMD_BYTE && channel == mSettings->mMosi)
	{
		SpiCmdData *cmd = reinterpret_cast<SpiCmdData *>(frame.mData2);
		if (frame.mData2 == 0x100)
			AddResultString("?"); // Not enough bits
		else if (frame.mData2 < 0x100)
		{
			// Normal byte and CMD=0xXX
			AnalyzerHelpers::GetNumberString(frame.mData2, display_base, 8, number_str, 128);
			AddResultString(number_str);
			AnalyzerHelpers::GetNumberString(frame.mData2, Hexadecimal, 8, number_str, 128);
			AddResultString("CMD=", number_str);
		}
		else
		{
			size_t i;
			U8 b = cmd->GetCode();
			AnalyzerHelpers::GetNumberString(b, display_base, 8, number_str, 128);
			AddResultString(number_str);
			AnalyzerHelpers::GetNumberString(b, Hexadecimal, 8, number_str, 128);
			AddResultString("CMD=", number_str);
			for (i = 0; i < cmd->mNames.size(); ++i)
				AddResultString(cmd->mNames[i].c_str());
			AddResultString(cmd->mNames[i - 1].c_str(), " CMD=", number_str);
		}
	}
	else if (frame.mType == FT_OUT_ADDR24 && channel == mSettings->mMosi)
	{
		AnalyzerHelpers::GetNumberString(frame.mData1, Hexadecimal, AddressBits(U32(frame.mData1 >> 24)),
			number_str, 128);
		AddResultString("A");
		AddResultString(number_str);
		AddResultString("A=", number_str);
	}
	else if ((frame.mType == FT_OUT_BYTE || frame.mType == FT_IN_OUT) && channel == mSettings->mMosi)
	{
		AnalyzerHelpers::GetNumberString(frame.mData1, display_base, 8, number_str, 128);
		AddResultString(number_str);
	}
	else if (frame.mType == FT_IN_REG && channel == mSettings->mMiso)
	{
		AddRegisterResult(reinterpret_cast<RegisterData *>(frame.mData1), frame.mData2, display_base);
	}
	else if (frame.mType == FT_OUT_REG && channel == mSettings->mMosi)
	{
		AddRegisterResult(reinterpret_cast<RegisterData *>(frame.mData2), frame.mData1, display_base);
	}
	else if ((frame.mType == FT_M) && channel == mSettings->mMosi)
	{
		AnalyzerHelpers::GetNumberString(frame.mData1, display_base, 8, number_str, 128);
		AddResultString("M");
		AddResultString(number_str);
		AnalyzerHelpers::GetNumberString(frame.mData1, Hexadecimal, 8, number_str, 128);
		AddResultString("M=", number_str);
	}
	else if ((frame.mType == FT_IN_BYTE || frame.mType == FT_IN_OUT) && channel == mSettings->mMiso)
	{
		AnalyzerHelpers::GetNumberString(frame.mData2, display_base, 8, number_str, 128);
		AddResultString(number_str);
	}
	else if (frame.mType == FT_DUMMY && channel == mSettings->mMosi)
	{
		AddResultString("x");
		AddResultString("Dummy");
	}
}

void SpiFlashAnalyzerResults::GenerateExportFile(const char* file, DisplayBase display_base, U32 export_type_user_id)
{
	std::ofstream file_stream(file, std::ios::out);

	U64 trigger_sample = mAnalyzer->GetTriggerSample();
	U32 sample_rate = mAnalyzer->GetSampleRate();

	file_stream << "Time [s],Value" << '\n';

	U64 num_frames = GetNumFrames();
	for (U32 i = 0; i < num_frames; i++)
	{
		Frame frame = GetFrame(i);

		char time_str[128];
		AnalyzerHelpers::GetTimeString(frame.mStartingSampleInclusive, trigger_sample, sample_rate, time_str, 128);

		char number_str[128];
		AnalyzerHelpers::GetNumberString(frame.mData1, display_base, 8, number_str, 128);

		file_stream << time_str << "," << number_str << '\n';

		if (UpdateExportProgressAndCheckForCancel(i, num_frames) == true)
		{
			file_stream.close();
			return;
		}
	}

	file_stream.close();
}

void SpiFlashAnalyzerResults::GenerateFrameTabularText(U64 frame_index, DisplayBase display_base)
{
	ClearTabularText();
	Frame frame = GetFrame(frame_index);

	char number_str[128];
	char number_str2[10];
	if (frame.mType == FT_CMD)
	{
		SpiCmdData *cmd = reinterpret_cast<SpiCmdData *>(frame.mData2);
		if (U64(cmd) > 0x100)
		{
			const char *s[4] = { 0 };
			if (cmd->mHasAddr)
			{
				U32 addr = U32(frame.mData1 >> 24);
				s[0] = "  A=";
				s[1] = number_str;
				AnalyzerHelpers::GetNumberString(addr, Hexadecimal, AddressBits(addr), number_str, 128);
			}
			if (cmd->mCmdOp == OP_DATA_READ || cmd->mCmdOp == OP_DATA_WRITE)
			{
				s[2] = "  bytes:";
				s[3] = number_str2;
				AnalyzerHelpers::GetNumberString(frame.mData1 & 0xFFFFFF, Decimal, 24, number_str2, 128);
			}
			// Add longest name with address and byte count if present
			AddTabularText(cmd->mNames.back().c_str(), s[0], s[1], s[2], s[3]);
		}
		else
		{
			if (frame.mData2 != 0x100)
			{
				AnalyzerHelpers::GetNumberString(frame.mData2, display_base, 8, number_str, 128);
				AddTabularText("?? CMD=", number_str);
			}
		}
	}
	else if (frame.mType == FT_OUT_ADDR24)
	{
		AnalyzerHelpers::GetNumberString(frame.mData1, Hexadecimal, AddressBits(U32(frame.mData1 >> 24)),
			number_str, 128);
		AddTabularText("A=", number_str);
	}
	else if ((frame.mType == FT_OUT_BYTE || frame.mType == FT_IN_OUT))
	{
		AnalyzerHelpers::GetNumberString(frame.mData1, display_base, 8, number_str, 128);
		AddTabularText(number_str);
}
	else if ((frame.mType == FT_M))
	{
		AnalyzerHelpers::GetNumberString(frame.mData1, Hexadecimal, 8, number_str, 128);
		AddTabularText("M=", number_str);
	}
	else if ((frame.mType == FT_IN_BYTE || frame.mType == FT_IN_OUT))
	{
		AnalyzerHelpers::GetNumberString(frame.mData2, display_base, 8, number_str, 128);
		AddTabularText(number_str);
	}
	else if (frame.mType == FT_DUMMY)
	{
		AddTabularText("Dummy");
	}
}

void SpiFlashAnalyzerResults::GeneratePacketTabularText(U64 packet_id, DisplayBase display_base)
{
	ClearResultStrings();
	AddResultString("not supported");
}

void SpiFlashAnalyzerResults::GenerateTransactionTabularText(U64 transaction_id, DisplayBase display_base)
{
	ClearResultStrings();
	AddResultString("not supported");
}
