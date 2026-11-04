#include "indi_openwheel.h"

#include "config.h"
#include "detect.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <unistd.h>

using namespace openwheel;

static std::unique_ptr<OpenWheel> driver(new OpenWheel());

namespace
{
std::string defaultName(int slot) { return "Filter " + std::to_string(slot + 1); }
}

OpenWheel::OpenWheel()
{
    setVersion(OPENWHEEL_VERSION_MAJOR, OPENWHEEL_VERSION_MINOR);
    wheel.log = [this](const std::string &m)
    {
        bool important = m.rfind("wheel warning", 0) == 0 || m.rfind("wheel fault", 0) == 0 ||
                         m.rfind("watchdog", 0) == 0 || m.rfind("connection lost", 0) == 0 ||
                         m.find("disconnected") != std::string::npos;
        if (important) LOGF_WARN("%s", m.c_str());
        else LOGF_DEBUG("%s", m.c_str());
    };
}

// ===================================================================== properties

bool OpenWheel::initProperties()
{
    INDI::FilterWheel::initProperties();

    PortTP[0].fill("PORT", "Port", "/dev/openwheel");
    PortTP.fill(getDeviceName(), "DEVICE_PORT", "Ports", CONNECTION_TAB, IP_RW, 60, IPS_IDLE);

    DetectSP[DETECT_AUTO].fill("AUTO", "Auto (USB ID)", ISS_ON);
    DetectSP[DETECT_MANUAL].fill("MANUAL", "Port above", ISS_OFF);
    DetectSP.fill(getDeviceName(), "OPENWHEEL_DETECT", "Find wheel", CONNECTION_TAB, IP_RW, ISR_1OFMANY, 60, IPS_IDLE);

    ActionSP[ACTION_HOME].fill("HOME", "Home", ISS_OFF);
    ActionSP[ACTION_HALT].fill("HALT", "Halt", ISS_OFF);
    ActionSP[ACTION_CALIBRATE].fill("CALIBRATE", "Calibrate", ISS_OFF);
    ActionSP[ACTION_CLEAR_ERROR].fill("CLEAR_ERROR", "Clear error", ISS_OFF);
    ActionSP.fill(getDeviceName(), "OPENWHEEL_ACTION", "Wheel", MAIN_CONTROL_TAB, IP_RW, ISR_ATMOST1, 60, IPS_IDLE);

    StatusTP[STATUS_STATE].fill("STATE", "State", "");
    StatusTP[STATUS_ERROR].fill("ERROR", "Error", "");
    StatusTP[STATUS_PROBLEM].fill("PROBLEM", "Problem", "");
    StatusTP.fill(getDeviceName(), "OPENWHEEL_STATUS", "Status", MAIN_CONTROL_TAB, IP_RO, 60, IPS_IDLE);

    HealthLP[HEALTH_CALIBRATED].fill("CALIBRATED", "Calibrated", IPS_IDLE);
    HealthLP[HEALTH_POSITION].fill("POSITION_KNOWN", "Position known", IPS_IDLE);
    HealthLP[HEALTH_FAULT].fill("FAULT", "No fault", IPS_IDLE);
    HealthLP[HEALTH_WARNING].fill("WARNING", "No warning", IPS_IDLE);
    HealthLP[HEALTH_EEPROM].fill("EEPROM", "EEPROM", IPS_IDLE);
    HealthLP.fill(getDeviceName(), "OPENWHEEL_HEALTH", "Health", MAIN_CONTROL_TAB, IPS_IDLE);

    OffsetNP.fill(getDeviceName(), "OPENWHEEL_FOCUS_OFFSETS", "Focus offsets", FILTER_TAB, IP_RW, 60, IPS_IDLE);

    InfoTP[INFO_MODEL].fill("MODEL", "Model", "");
    InfoTP[INFO_FIRMWARE].fill("FIRMWARE", "Firmware", "");
    InfoTP[INFO_PROTOCOL].fill("PROTOCOL", "Protocol", "");
    InfoTP[INFO_SERIAL].fill("SERIAL", "Serial number", "");
    InfoTP[INFO_PORT].fill("PORT", "Port", "");
    InfoTP.fill(getDeviceName(), "OPENWHEEL_INFO", "Wheel", INFO_TAB, IP_RO, 60, IPS_IDLE);

    HousekeepingNP[HK_MCU_TEMP].fill("MCU_TEMP", "MCU temperature (C)", "%.1f", -60, 150, 0, 0);
    HousekeepingNP[HK_VDDA].fill("VDDA", "MCU supply (V)", "%.3f", 0, 4, 0, 0);
    HousekeepingNP[HK_V5].fill("V5", "+5V rail (V, -1 n/a)", "%.3f", -1, 6, 0, -1);
    HousekeepingNP[HK_V5_MIN].fill("V5_MIN", "+5V min. last move (V)", "%.3f", -1, 6, 0, -1);
    HousekeepingNP[HK_DRIVER_TEMP].fill("DRIVER_TEMP", "Driver temp. class (C)", "%.0f", -1, 200, 0, 0);
    HousekeepingNP[HK_WARNINGS].fill("WARNINGS", "Warning bits", "%.0f", 0, 255, 0, 0);
    HousekeepingNP.fill(getDeviceName(), "OPENWHEEL_HOUSEKEEPING", "Housekeeping", INFO_TAB, IP_RO, 60, IPS_IDLE);

    WarningTP[0].fill("TEXT", "Warnings", "none");
    WarningTP.fill(getDeviceName(), "OPENWHEEL_WARNINGS", "Warnings", INFO_TAB, IP_RO, 60, IPS_IDLE);

    addDebugControl();
    return true;
}

void OpenWheel::ISGetProperties(const char *dev)
{
    INDI::FilterWheel::ISGetProperties(dev);
    defineProperty(PortTP);
    defineProperty(DetectSP);
    loadConfig(true, PortTP.getName());
    loadConfig(true, DetectSP.getName());
}

bool OpenWheel::updateProperties()
{
    if (isConnected())
    {
        buildFilterProperties();
        INDI::FilterWheel::updateProperties();   // defines FILTER_SLOT and FILTER_NAME
        defineProperty(ActionSP);
        defineProperty(StatusTP);
        defineProperty(HealthLP);
        if (OffsetNP.size() > 0) defineProperty(OffsetNP);
        defineProperty(InfoTP);
        if (wheel.housekeepingSupported())
        {
            defineProperty(HousekeepingNP);
            defineProperty(WarningTP);
        }
        publishInfo();
        publishStatus();
        publishHousekeeping();
    }
    else
    {
        INDI::FilterWheel::updateProperties();
        deleteProperty(ActionSP);
        deleteProperty(StatusTP);
        deleteProperty(HealthLP);
        deleteProperty(OffsetNP);
        deleteProperty(InfoTP);
        deleteProperty(HousekeepingNP);
        deleteProperty(WarningTP);
        definedSlots = -1;
    }
    return true;
}

void OpenWheel::buildFilterProperties()
{
    const Status &st = wheel.status();
    int slots = st.slots;
    FilterSlotNP[0].setMin(1);
    FilterSlotNP[0].setMax(slots > 0 ? slots : 1);
    if (st.position() >= 0) CurrentFilter = st.position() + 1;
    FilterSlotNP[0].setValue(CurrentFilter <= slots ? CurrentFilter : 1);

    if (wheel.configValid())
    {
        // The names stored in the wheel are the truth; INDI's config file only mirrors them.
        FilterNameTP.resize(0);
        const auto &names = wheel.filterNames();
        for (int i = 0; i < slots; i++)
        {
            char name[MAXINDINAME], label[MAXINDILABEL];
            snprintf(name, sizeof(name), "FILTER_SLOT_NAME_%d", i + 1);
            snprintf(label, sizeof(label), "Filter#%d", i + 1);
            INDI::WidgetText t;
            t.fill(name, label, names[i].empty() ? defaultName(i) : names[i]);
            FilterNameTP.push(std::move(t));
        }
        FilterNameTP.fill(getDeviceName(), "FILTER_NAME", "Filter", FILTER_TAB, IP_RW, 0, IPS_IDLE);
        FilterNameTP.shrink_to_fit();

        OffsetNP.resize(0);
        const auto &off = wheel.focusOffsets();
        for (int i = 0; i < slots; i++)
        {
            char name[MAXINDINAME], label[MAXINDILABEL];
            snprintf(name, sizeof(name), "OFFSET_%d", i + 1);
            snprintf(label, sizeof(label), "Slot %d", i + 1);
            INDI::WidgetNumber w;
            w.fill(name, label, "%.0f", -32768, 32767, 1, off[i]);
            OffsetNP.push(std::move(w));
        }
        OffsetNP.fill(getDeviceName(), "OPENWHEEL_FOCUS_OFFSETS", "Focus offsets", FILTER_TAB, IP_RW, 60, IPS_IDLE);
        OffsetNP.shrink_to_fit();
    }
    else
    {
        OffsetNP.resize(0);   // firmware without name storage: names come from INDI's config file
    }
    definedSlots = slots;
}

bool OpenWheel::GetFilterNames()
{
    if (wheel.configValid()) return true;   // filled by buildFilterProperties()
    return INDI::FilterWheel::GetFilterNames();
}

bool OpenWheel::SetFilterNames()
{
    if (!wheel.connected() || !wheel.configSupported())
        return INDI::FilterWheel::SetFilterNames();   // old firmware: keep the names in INDI's config

    const auto &old = wheel.filterNames();
    std::vector<std::string> names;
    for (size_t i = 0; i < FilterNameTP.size(); i++)
    {
        std::string typed = FilterNameTP[i].getText() ? FilterNameTP[i].getText() : "";
        int slot = static_cast<int>(i);
        // "Filter N" is only how an empty entry is shown: keep it empty in the wheel
        if (typed == defaultName(slot) && slot < static_cast<int>(old.size()) && old[slot].empty()) typed.clear();
        std::string clean = sanitizeName(typed);
        if (clean != typed)
            LOGF_WARN("Filter %d: \"%s\" stored as \"%s\" (max 15 characters, none of , # : |).", slot + 1, typed.c_str(), clean.c_str());
        names.push_back(clean);
    }
    std::vector<int> offsets = wheel.focusOffsets();
    offsets.resize(names.size(), 0);
    names.resize(offsets.size());

    Result r = wheel.saveFilterConfig(names, offsets);
    if (!r.ok) LOGF_ERROR("Saving the filter names in the wheel failed: %s", r.message.c_str());
    else LOG_INFO("Filter names saved in the wheel.");

    // show what the wheel really holds
    const auto &now = wheel.filterNames();
    for (size_t i = 0; i < FilterNameTP.size() && i < now.size(); i++)
        FilterNameTP[i].setText(now[i].empty() ? defaultName(static_cast<int>(i)) : now[i]);
    if (r.ok) saveConfig(FilterNameTP);
    return r.ok;
}

// ===================================================================== client requests

bool OpenWheel::ISNewText(const char *dev, const char *name, char *texts[], char *names[], int n)
{
    if (dev && !strcmp(dev, getDeviceName()))
    {
        if (PortTP.isNameMatch(name))
        {
            PortTP.update(texts, names, n);
            PortTP.setState(IPS_OK);
            PortTP.apply();
            saveConfig(PortTP);
            return true;
        }
        if (!strcmp(name, "FILTER_NAME") && isConnected() && wheel.configSupported() && isConfigLoading())
        {
            FilterNameTP.apply();   // the wheel's own names win over the config file
            return true;
        }
    }
    return INDI::FilterWheel::ISNewText(dev, name, texts, names, n);
}

bool OpenWheel::ISNewNumber(const char *dev, const char *name, double values[], char *names[], int n)
{
    if (dev && !strcmp(dev, getDeviceName()) && OffsetNP.isNameMatch(name))
    {
        if (isConfigLoading() || !isConnected()) { OffsetNP.apply(); return true; }
        std::vector<int> offsets = wheel.focusOffsets();
        for (int i = 0; i < n; i++)
        {
            int idx = OffsetNP.findWidgetIndexByName(names[i]);
            if (idx >= 0 && idx < static_cast<int>(offsets.size())) offsets[idx] = static_cast<int>(values[i]);
        }
        Result r = wheel.saveFilterConfig(wheel.filterNames(), offsets);
        const auto &now = wheel.focusOffsets();
        for (size_t i = 0; i < OffsetNP.size() && i < now.size(); i++) OffsetNP[i].setValue(now[i]);
        if (r.ok)
        {
            OffsetNP.setState(IPS_OK);
            OffsetNP.apply();
            LOG_INFO("Focus offsets saved in the wheel.");
        }
        else
        {
            OffsetNP.setState(IPS_ALERT);
            OffsetNP.apply("Saving the focus offsets failed: %s", r.message.c_str());
        }
        return true;
    }
    return INDI::FilterWheel::ISNewNumber(dev, name, values, names, n);
}

bool OpenWheel::ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int n)
{
    if (dev && !strcmp(dev, getDeviceName()))
    {
        if (DetectSP.isNameMatch(name))
        {
            DetectSP.update(states, names, n);
            DetectSP.setState(IPS_OK);
            DetectSP.apply();
            saveConfig(DetectSP);
            return true;
        }
        if (ActionSP.isNameMatch(name))
        {
            ActionSP.reset();   // a button left On by the last action must not win over the new one
            ActionSP.update(states, names, n);
            int idx = ActionSP.findOnSwitchIndex();
            ActionSP.reset();
            if (idx < 0) { ActionSP.setState(IPS_IDLE); ActionSP.apply(); return true; }
            runAction(idx);
            return true;
        }
    }
    return INDI::FilterWheel::ISNewSwitch(dev, name, states, names, n);
}

bool OpenWheel::runAction(int index)
{
    if (!isConnected()) { ActionSP.setState(IPS_ALERT); ActionSP.apply("Not connected."); return false; }
    Result r;
    switch (index)
    {
        case ACTION_HOME:
            r = wheel.home();
            if (r.ok)
            {
                activity = Homing;
                TargetFilter = 1;
                FilterSlotNP.setState(IPS_BUSY);
                FilterSlotNP.apply();
                ActionSP[ACTION_HOME].setState(ISS_ON);
                ActionSP.setState(IPS_BUSY);
                ActionSP.apply("Homing...");
            }
            break;
        case ACTION_HALT:
            r = wheel.halt();
            if (r.ok)
            {
                // halt() has read the status again: a wheel that was already standing keeps its position
                activity = None;
                int pos = wheel.status().position();
                if (pos >= 0)
                {
                    CurrentFilter = pos + 1;
                    SelectFilterDone(CurrentFilter);
                }
                else
                {
                    FilterSlotNP.setState(IPS_ALERT);
                    FilterSlotNP.apply("Wheel halted: position unknown, home it or select a filter.");
                }
                ActionSP.setState(IPS_OK);
                ActionSP.apply("Halted.");
            }
            break;
        case ACTION_CALIBRATE:
            r = wheel.calibrate();
            if (r.ok)
            {
                activity = Calibrating;
                FilterSlotNP.setState(IPS_BUSY);
                FilterSlotNP.apply();
                ActionSP[ACTION_CALIBRATE].setState(ISS_ON);
                ActionSP.setState(IPS_BUSY);
                ActionSP.apply("Calibrating: the wheel turns a few times to find its slots (up to 2 minutes)...");
            }
            break;
        case ACTION_CLEAR_ERROR:
            r = wheel.clearError();
            if (r.ok)
            {
                ActionSP.setState(IPS_OK);
                ActionSP.apply("Error cleared.");
            }
            break;
    }
    if (!r.ok)
    {
        ActionSP.setState(IPS_ALERT);
        ActionSP.apply("%s failed: %s", ActionSP[index].getLabel(), r.message.c_str());
    }
    if (!wheel.connected()) { connectionLost(); return false; }
    publishStatus();
    return r.ok;
}

int OpenWheel::QueryFilter()
{
    int pos = wheel.status().position();
    if (pos >= 0) CurrentFilter = pos + 1;
    return CurrentFilter;
}

bool OpenWheel::SelectFilter(int position)
{
    Result r = wheel.moveTo(position - 1);
    if (!r.ok)
    {
        LOGF_ERROR("Cannot move to filter %d: %s", position, r.message.c_str());
        if (!wheel.connected()) connectionLost();
        return false;
    }
    TargetFilter = position;
    const Status &st = wheel.status();
    if (!st.busy() && st.position() == position - 1)
    {
        activity = None;
        CurrentFilter = position;
        SelectFilterDone(position);   // already there
    }
    else
        activity = Moving;
    publishStatus();
    return true;
}

// ===================================================================== connection

bool OpenWheel::openWheel(std::string &why)
{
    std::string configured = PortTP[0].getText() ? PortTP[0].getText() : "";
    if (DetectSP.findOnSwitchIndex() == DETECT_MANUAL)
    {
        if (configured.empty()) { why = "no port set"; return false; }
        return wheel.open(configured, why);
    }

    // Auto: USB VID/PID first, then the :32# handshake. A wheel already named in the port field wins.
    std::vector<UsbPort> found = findCandidates();
    std::stable_sort(found.begin(), found.end(), [&](const UsbPort &a, const UsbPort &b)
    {
        bool pa = a.device == configured || a.byId == configured;
        bool pb = b.device == configured || b.byId == configured;
        return pa && !pb;
    });
    std::string failures;
    for (const auto &c : found)
    {
        std::string path = c.byId.empty() ? c.device : c.byId;
        std::string w;
        if (wheel.open(path, w))
        {
            PortTP[0].setText(path);
            PortTP.setState(IPS_OK);
            PortTP.apply();
            return true;
        }
        LOGF_WARN("Skipping %s: %s", path.c_str(), w.c_str());
        failures += (failures.empty() ? "" : "; ") + w;
    }
    // No wheel on USB: a port the user entered (a symlink, the simulator, a serial adapter) still
    // works, but only if it answers the identify handshake.
    if (found.empty() && !configured.empty() && access(configured.c_str(), F_OK) == 0)
    {
        if (wheel.open(configured, why)) return true;
        failures = why;
    }
    why = found.empty() && failures.empty()
          ? "no OpenWheel found on USB (VID 1209, PID 2180 or 0001). Check the cable, or choose a port and \"Port above\"."
          : failures;
    return false;
}

bool OpenWheel::Connect()
{
    if (wheel.connected()) return true;
    std::string why;
    if (!openWheel(why))
    {
        LOGF_ERROR("Connection failed: %s", why.c_str());
        return false;
    }
    activity = wheel.status().busy() ? Moving : None;
    LOGF_INFO("Connected to %s on %s.", wheel.identity().toString().c_str(), wheel.portName().c_str());
    if (!wheel.problem().empty()) LOGF_WARN("%s", wheel.problem().c_str());
    scheduleTimer();
    return true;
}

bool OpenWheel::Disconnect()
{
    if (timerId >= 0) RemoveTimer(timerId);
    timerId = -1;
    wheel.close();
    activity = None;
    LOG_INFO("Disconnected.");
    return true;
}

void OpenWheel::connectionLost()
{
    LOGF_ERROR("%s", wheel.problem().c_str());
    if (timerId >= 0) RemoveTimer(timerId);
    timerId = -1;
    wheel.close();
    activity = None;
    setConnected(false, IPS_ALERT, wheel.problem().c_str());
    updateProperties();
}

void OpenWheel::scheduleTimer()
{
    int ms = wheel.nextServiceInMs();
    if (ms < 50) ms = 50;
    timerId = SetTimer(static_cast<uint32_t>(ms));
}

void OpenWheel::TimerHit()
{
    timerId = -1;
    if (!isConnected() || !wheel.connected()) return;

    bool changed = wheel.service();
    if (!wheel.connected()) { connectionLost(); return; }

    trackActivity();

    // recalibrated to another slot count: redefine the filter properties
    const Status &st = wheel.status();
    if (!st.busy() && st.slots != definedSlots && (wheel.configValid() || !wheel.configSupported()))
    {
        LOGF_INFO("The wheel has %d slots now.", st.slots);
        deleteProperty(FilterSlotNP);
        deleteProperty(FilterNameTP);
        deleteProperty(OffsetNP);
        buildFilterProperties();
        defineProperty(FilterSlotNP);
        if (FilterNameTP.size() == 0) GetFilterNames();
        defineProperty(FilterNameTP);
        if (OffsetNP.size() > 0) defineProperty(OffsetNP);
    }

    if (changed)
    {
        publishStatus();
        publishHousekeeping();
    }
    scheduleTimer();
}

void OpenWheel::trackActivity()
{
    const Status &st = wheel.status();
    int pos = st.position();

    if (activity == None)
    {
        if (st.busy() && st.state != WheelState::Init)
        {
            // moved by something else (the button on the wheel, a watchdog-free move...)
            activity = st.state == WheelState::Calibrating ? Calibrating : st.state == WheelState::Homing ? Homing : Moving;
            TargetFilter = st.target >= 0 ? st.target + 1 : 1;
            FilterSlotNP.setState(IPS_BUSY);
            FilterSlotNP.apply();
        }
        else if (pos >= 0 && pos + 1 != static_cast<int>(FilterSlotNP[0].getValue()))
        {
            CurrentFilter = pos + 1;
            SelectFilterDone(CurrentFilter);
        }
        return;
    }
    if (st.busy()) return;

    Activity was = activity;
    activity = None;
    if (was != Moving) ActionSP.reset();   // Home / Calibrate button pops out when done
    if (st.state == WheelState::Error || (st.error != 0 && pos < 0))
    {
        FilterSlotNP.setState(IPS_ALERT);
        FilterSlotNP.apply("Wheel fault: %s", describeError(st.error).c_str());
        if (was != Moving) { ActionSP.setState(IPS_ALERT); ActionSP.apply(); }
        return;
    }
    if (pos < 0)
    {
        FilterSlotNP.setState(IPS_ALERT);
        FilterSlotNP.apply("%s", wheel.problem().empty() ? "Wheel stopped, position unknown." : wheel.problem().c_str());
        if (was != Moving) { ActionSP.setState(IPS_ALERT); ActionSP.apply(); }
        return;
    }
    CurrentFilter = pos + 1;
    if (was == Moving && CurrentFilter != TargetFilter)
    {
        FilterSlotNP[0].setValue(CurrentFilter);
        FilterSlotNP.setState(IPS_ALERT);
        FilterSlotNP.apply("Wheel stopped at filter %d instead of %d.", CurrentFilter, TargetFilter);
        return;
    }
    SelectFilterDone(CurrentFilter);
    if (was == Homing) { ActionSP.setState(IPS_OK); ActionSP.apply("Home found."); }
    if (was == Calibrating)
    {
        ActionSP.setState(IPS_OK);
        ActionSP.apply("Calibration done: %d slots.", st.slots);
    }
}

// ===================================================================== publishing

void OpenWheel::publishStatus()
{
    const Status &st = wheel.status();
    std::string state = stateName(st.state);
    if (st.state == WheelState::Moving && st.target >= 0) state += " to filter " + std::to_string(st.target + 1);
    else if (st.position() >= 0) state += " at filter " + std::to_string(st.position() + 1);
    std::string err = st.error ? std::to_string(st.error) + ": " + describeError(st.error) : "none";
    std::string prob = wheel.problem().empty() ? "none" : wheel.problem();

    IPState s = IPS_OK;
    if (st.busy()) s = IPS_BUSY;
    if (st.error || !wheel.problem().empty()) s = IPS_ALERT;

    bool textChanged = state != StatusTP[STATUS_STATE].getText() || err != StatusTP[STATUS_ERROR].getText() ||
                       prob != StatusTP[STATUS_PROBLEM].getText() || s != StatusTP.getState();
    if (textChanged)
    {
        StatusTP[STATUS_STATE].setText(state);
        StatusTP[STATUS_ERROR].setText(err);
        StatusTP[STATUS_PROBLEM].setText(prob);
        StatusTP.setState(s);
        StatusTP.apply();
    }

    bool warn = st.hasWarning() || (wheel.housekeepingValid() && wheel.housekeeping().warnings != 0);
    IPState l[5] =
    {
        st.calibrated() ? IPS_OK : IPS_ALERT,
        st.busy() ? IPS_BUSY : st.positionKnown() ? IPS_OK : IPS_IDLE,
        st.error ? IPS_ALERT : IPS_OK,
        warn ? IPS_BUSY : IPS_OK,
        st.eepromFault() ? IPS_ALERT : IPS_OK,
    };
    bool lightsChanged = false;
    for (int i = 0; i < 5; i++)
        if (HealthLP[i].getState() != l[i]) { HealthLP[i].setState(l[i]); lightsChanged = true; }
    IPState overall = (st.error || st.eepromFault() || !st.calibrated()) ? IPS_ALERT : warn ? IPS_BUSY : IPS_OK;
    if (lightsChanged || HealthLP.getState() != overall)
    {
        HealthLP.setState(overall);
        HealthLP.apply();
    }
}

void OpenWheel::publishHousekeeping()
{
    if (!wheel.housekeepingSupported() || !wheel.housekeepingValid()) return;
    const Housekeeping &h = wheel.housekeeping();
    HousekeepingNP[HK_MCU_TEMP].setValue(h.mcuTemp);
    HousekeepingNP[HK_VDDA].setValue(h.vdda);
    HousekeepingNP[HK_V5].setValue(h.v5);
    HousekeepingNP[HK_V5_MIN].setValue(h.v5min);
    HousekeepingNP[HK_DRIVER_TEMP].setValue(h.driverTemp);
    HousekeepingNP[HK_WARNINGS].setValue(h.warnings);
    HousekeepingNP.setState(h.warnings ? IPS_BUSY : IPS_OK);
    HousekeepingNP.apply();

    std::string w = Housekeeping::describeWarnings(h.warnings);
    if (w != WarningTP[0].getText())
    {
        WarningTP[0].setText(w);
        WarningTP.setState(h.warnings ? IPS_BUSY : IPS_OK);
        WarningTP.apply();
    }
}

void OpenWheel::publishInfo()
{
    const Identity &id = wheel.identity();
    InfoTP[INFO_MODEL].setText(id.model);
    InfoTP[INFO_FIRMWARE].setText(id.firmware);
    InfoTP[INFO_PROTOCOL].setText(std::to_string(id.protocol));
    InfoTP[INFO_SERIAL].setText(id.serial);
    InfoTP[INFO_PORT].setText(wheel.portName());
    InfoTP.setState(IPS_OK);
    InfoTP.apply();
}

bool OpenWheel::saveConfigItems(FILE *fp)
{
    INDI::FilterWheel::saveConfigItems(fp);
    PortTP.save(fp);
    DetectSP.save(fp);
    return true;
}
