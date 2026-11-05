// INDI driver for the OpenWheel filter wheel.
//
// The serial session (bounded timeouts, failure counting, watchdog, names/offsets cache) lives in
// wheel.h; this class maps it onto INDI properties. It does not use INDI's serial connection
// plugin: its "auto search" would send our commands to every serial port, a mount included.
// Instead the wheel is found by its USB VID/PID and confirmed with :32#, or opened on a fixed port.
#pragma once

#include "wheel.h"

#include <libindi/indifilterwheel.h>
#include <libindi/indipropertylight.h>
#include <libindi/indipropertynumber.h>
#include <libindi/indipropertyswitch.h>
#include <libindi/indipropertytext.h>

class OpenWheel : public INDI::FilterWheel
{
    public:
        OpenWheel();

        const char *getDefaultName() override { return "OpenWheel"; }
        bool initProperties() override;
        void ISGetProperties(const char *dev) override;
        bool updateProperties() override;
        bool ISNewNumber(const char *dev, const char *name, double values[], char *names[], int n) override;
        bool ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int n) override;
        bool ISNewText(const char *dev, const char *name, char *texts[], char *names[], int n) override;

    protected:
        bool Connect() override;
        bool Disconnect() override;
        void TimerHit() override;
        bool saveConfigItems(FILE *fp) override;

        int QueryFilter() override;
        bool SelectFilter(int position) override;
        bool SetFilterNames() override;
        bool GetFilterNames() override;

    private:
        enum Activity { None, Moving, Homing, Calibrating };

        bool openWheel(std::string &why);
        void connectionLost();
        void scheduleTimer();

        void buildFilterProperties();     // slot range, names and offsets from the wheel
        void publishStatus();
        void publishHousekeeping();
        void publishInfo();
        void trackActivity();
        bool runAction(int index);

        openwheel::Wheel wheel;
        Activity activity = None;
        int definedSlots = -1;
        int timerId = -1;

        // Connection
        INDI::PropertyText PortTP {1};
        INDI::PropertySwitch DetectSP {2};
        enum { DETECT_AUTO, DETECT_MANUAL };

        // Main tab
        INDI::PropertySwitch ActionSP {4};
        enum { ACTION_HOME, ACTION_HALT, ACTION_CALIBRATE, ACTION_CLEAR_ERROR };
        INDI::PropertyText StatusTP {3};
        enum { STATUS_STATE, STATUS_ERROR, STATUS_PROBLEM };
        INDI::PropertyLight HealthLP {5};
        enum { HEALTH_CALIBRATED, HEALTH_POSITION, HEALTH_FAULT, HEALTH_WARNING, HEALTH_EEPROM };

        // Filter tab
        INDI::PropertyNumber OffsetNP {0};

        // Info tab
        INDI::PropertyText InfoTP {5};
        enum { INFO_MODEL, INFO_FIRMWARE, INFO_PROTOCOL, INFO_SERIAL, INFO_PORT };
        INDI::PropertyNumber HousekeepingNP {6};
        enum { HK_MCU_TEMP, HK_VDDA, HK_V5, HK_V5_MIN, HK_DRIVER_TEMP, HK_WARNINGS };
        INDI::PropertyText WarningTP {1};
};
