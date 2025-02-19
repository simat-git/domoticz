#include "stdafx.h"
#include "KMTronic433.h"
#include "../main/Logger.h"
#include "../main/Helper.h"
#include "../main/RFXtrx.h"
#include "../main/localtime_r.h"
#include "P1MeterBase.h"
#include "hardwaretypes.h"
#include <string>
#include <algorithm>
#include <iostream>
#include <boost/exception/diagnostic_information.hpp>

#include <ctime>

//#define DEBUG_KMTronic

#define RETRY_DELAY 30

KMTronic433::KMTronic433(const int ID, const std::string& devname)
{
	m_HwdID = ID;
	m_szSerialPort = devname;
	m_iBaudRate = 9600;
	m_iQueryState = 0;
	m_bHaveReceived = false;
	m_retrycntr = RETRY_DELAY;
}

bool KMTronic433::StartHardware()
{
	RequestStart();

	m_bDoInitialQuery = true;
	m_iQueryState = 0;

	m_retrycntr = RETRY_DELAY; //will force reconnect first thing

	//Start worker thread
	m_thread = std::make_shared<std::thread>([this] { Do_Work(); });
	SetThreadNameInt(m_thread->native_handle());

	return (m_thread != nullptr);
}

bool KMTronic433::StopHardware()
{
	if (m_thread)
	{
		RequestStop();
		m_thread->join();
		m_thread.reset();
	}
	m_bIsStarted = false;
	return true;
}

void KMTronic433::Do_Work()
{
	int sec_counter = 0;

	while (!IsStopRequested(1000))
	{
		sec_counter++;

		if (sec_counter % 12 == 0) {
			m_LastHeartbeat = mytime(nullptr);
		}

		if (!isOpen())
		{
			if (m_retrycntr == 0)
			{
				Log(LOG_STATUS, "retrying in %d seconds...", RETRY_DELAY);
			}
			m_retrycntr++;
			if (m_retrycntr >= RETRY_DELAY)
			{
				m_retrycntr = 0;
				if (OpenSerialDevice())
				{
					GetRelayStates();
				}
			}
		}
	}
	terminate();

	Log(LOG_STATUS, "Worker stopped...");
}

bool KMTronic433::OpenSerialDevice()
{
	//Try to open the Serial Port
	try
	{
		Log(LOG_STATUS, "Using serial port: %s", m_szSerialPort.c_str());
#ifndef WIN32
		openOnlyBaud(
			m_szSerialPort,
			m_iBaudRate,
			boost::asio::serial_port_base::parity(boost::asio::serial_port_base::parity::none),
			boost::asio::serial_port_base::character_size(8)
			);
#else
		open(
			m_szSerialPort,
			m_iBaudRate,
			boost::asio::serial_port_base::parity(boost::asio::serial_port_base::parity::none),
			boost::asio::serial_port_base::character_size(8)
			);
#endif
	}
	catch (boost::exception & e)
	{
		Log(LOG_ERROR, "Error opening serial port!");
#ifdef _DEBUG
		Log(LOG_ERROR, "-----------------\n%s\n-----------------", boost::diagnostic_information(e).c_str());
#else
		(void)e;
#endif
		return false;
	}
	catch (...)
	{
		Log(LOG_ERROR, "Error opening serial port!!!");
		return false;
	}
	m_bIsStarted = true;
	m_bufferpos = 0;
	setReadCallback([this](auto d, auto l) { readCallback(d, l); });
	sOnConnected(this);
	return true;
}

void KMTronic433::readCallback(const char *data, size_t len)
{
	if (!m_bIsStarted)
		return;

	if (len > sizeof(m_buffer))
		return;

	m_bHaveReceived = true;

	if (!m_bEnableReceive)
		return; //receiving not enabled

	memcpy(m_buffer, data, len);
	m_bufferpos = (int)len;
}

bool KMTronic433::WriteInt(const unsigned char *data, const size_t len, const bool bWaitForReturn)
{
	if (!isOpen())
		return false;
	m_bHaveReceived = false;
	write((const char*)data, len);
	if (!bWaitForReturn)
		return true;
	sleep_milliseconds(100);
	return (m_bHaveReceived == true);
}

void KMTronic433::GetRelayStates()
{
	m_TotRelais = 8;
	for (int ii = 0; ii < m_TotRelais; ii++)
	{
		std::stringstream sstr;
		sstr << "Relay " << (ii + 1);
		bool bIsOn = false;
		SendSwitchIfNotExists(ii + 1, 1, 255, bIsOn, (bIsOn) ? 100 : 0, sstr.str(), m_Name);
	}
}
