// based on: https://github.com/DomiStyle/esphome-panasonic-ac
#include "esphome/components/climate/climate.h"
#include "esphome/components/climate/climate_mode.h"
#include "esppac.h"

namespace esphome {
namespace sinclair_ac {
namespace CNT {

enum class ACState {
    Initializing, /* no data for quite a long time */
    Ready,        /* AC talking to us */
};

enum class ACUpdate {
    NoUpdate,    /* no parameters changed - normally process data, static flag set */
    UpdateStart, /* start update with 0xAF and cleared static flag */
    UpdateClear, /* update without 0xAF and cleared static flag */
};

namespace protocol {
    /* SYNC */
    static const uint8_t SYNC                = 0x7E;
    /* packet types */
    static const uint8_t CMD_IN_UNIT_REPORT  = 0x31;
    static const uint8_t CMD_OUT_PARAMS_SET  = 0x01;
    static const uint8_t CMD_OUT_SYNC_TIME   = 0x03;
    static const uint8_t CMD_OUT_MAC_REPORT  = 0x04; /* 7e 7e 0d 04 04 00 00 00 AA BB CC DD EE FF 00 -> AA BB CC DD EE FF = MAC address */
    static const uint8_t CMD_OUT_UNKNOWN_1   = 0x02; /* 7e 7e 10 02 00 00 00 00 00 00 01 00 28 1e 19 23 23 00 b8 */
    static const uint8_t CMD_IN_UNKNOWN_1    = 0x44; /* 7e 7e 1a 44 01 00 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 01 */
    static const uint8_t CMD_IN_UNKNOWN_2    = 0x33; /* 7e 7e 2f 33 00 00 40 00 09 20 19 0a 00 10 00 14 17 5b 08 08 00 00 00 00 00 00 00 00 01 00 00 0d 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 */

    /* byte indexes are AFTER we remove first 4 bytes from the packet (sync, length, type) as well as a checksum */
    /* unit report packet data fields, for binary values there is no need to define bit offset/position */
    static const uint8_t REPORT_PWR_BYTE       = 4;
    static const uint8_t REPORT_PWR_MASK       = 0b10000000;

    static const uint8_t REPORT_MODE_BYTE      = 4;
    static const uint8_t REPORT_MODE_MASK      = 0b01110000;
    static const uint8_t REPORT_MODE_POS       = 4;
    static const uint8_t REPORT_MODE_AUTO          = 0;
    static const uint8_t REPORT_MODE_COOL          = 1;
    static const uint8_t REPORT_MODE_DRY           = 2;
    static const uint8_t REPORT_MODE_FAN           = 3;
    static const uint8_t REPORT_MODE_HEAT          = 4;

    static const uint8_t REPORT_FAN_SPD1_BYTE  = 18;
    static const uint8_t REPORT_FAN_SPD1_MASK  = 0b00001111;
    static const uint8_t REPORT_FAN_SPD1_POS   = 0;
    static const uint8_t REPORT_FAN_SPD2_BYTE  = 4;
    static const uint8_t REPORT_FAN_SPD2_MASK  = 0b00000011;
    static const uint8_t REPORT_FAN_SPD2_POS   = 0;
    static const uint8_t REPORT_FAN_QUIET_BYTE = 16;
    static const uint8_t REPORT_FAN_QUIET_MASK = 0b00001000;
    static const uint8_t REPORT_FAN_TURBO_BYTE = 6;
    static const uint8_t REPORT_FAN_TURBO_MASK = 0b00000001;

    static const uint8_t REPORT_TEMP_SET_BYTE  = 5;
    static const uint8_t REPORT_TEMP_SET_MASK  = 0b11110000;
    static const uint8_t REPORT_TEMP_SET_POS   = 4;
    static const uint8_t REPORT_TEMP_SET_OFF   = 16; /* temperature offset from value in packet */

    static const uint8_t REPORT_TEMP_ACT_BYTE  = 42;
    static const uint8_t REPORT_TEMP_ACT_MASK  = 0b11111111;
    static const uint8_t REPORT_TEMP_ACT_POS   = 0;
    static const uint8_t REPORT_TEMP_ACT_OFF   = 16;  /* temperature offset from value in packet */
    static const float   REPORT_TEMP_ACT_DIV   = 2.0; /* temperature divider from value in packet */
    static const uint8_t REPORT_TEMP_ACT_OFF_GREE = 40; /* Gree-based units: temperature = raw - 40 (current_temperature_formula: gree) */

    static const uint8_t REPORT_HSWING_BYTE    = 8;
    static const uint8_t REPORT_HSWING_MASK    = 0b00000111;
    static const uint8_t REPORT_HSWING_POS     = 0;
    static const uint8_t REPORT_HSWING_OFF         = 0;
    static const uint8_t REPORT_HSWING_FULL        = 1;
    static const uint8_t REPORT_HSWING_CLEFT       = 2;
    static const uint8_t REPORT_HSWING_CMIDL       = 3;
    static const uint8_t REPORT_HSWING_CMID        = 4;
    static const uint8_t REPORT_HSWING_CMIDR       = 5;
    static const uint8_t REPORT_HSWING_CRIGHT      = 6;

    static const uint8_t REPORT_VSWING_BYTE    = 8;
    static const uint8_t REPORT_VSWING_MASK    = 0b11110000;
    static const uint8_t REPORT_VSWING_POS     = 4;
    static const uint8_t REPORT_VSWING_OFF         = 0;
    static const uint8_t REPORT_VSWING_FULL        = 1;
    static const uint8_t REPORT_VSWING_CUP         = 2;
    static const uint8_t REPORT_VSWING_CMIDU       = 3;
    static const uint8_t REPORT_VSWING_CMID        = 4;
    static const uint8_t REPORT_VSWING_CMIDD       = 5;
    static const uint8_t REPORT_VSWING_CDOWN       = 6;
    static const uint8_t REPORT_VSWING_DOWN        = 7;
    static const uint8_t REPORT_VSWING_MIDD        = 8;
    static const uint8_t REPORT_VSWING_MID         = 9;
    static const uint8_t REPORT_VSWING_MIDU        = 10;
    static const uint8_t REPORT_VSWING_UP          = 11;

    static const uint8_t REPORT_DISP_ON_BYTE   = 6;
    static const uint8_t REPORT_DISP_ON_MASK   = 0b00000010;
    static const uint8_t REPORT_DISP_MODE_BYTE = 9;
    static const uint8_t REPORT_DISP_MODE_MASK = 0b00110000;
    static const uint8_t REPORT_DISP_MODE_POS  = 4;

    /* I FEEL (seen in reports of a Coolexpert ACH-09BI while toggling I FEEL on the remote):
       byte 9 bit6 = I FEEL active, byte 24 = temperature measured by the remote in C,
       byte 42 then follows the remote's value instead of the unit's own sensor.
       Tested: setting the flag and a temperature in SET packets (with and without 0xAF) is
       ignored by the unit, I FEEL only works from the IR remote. */
    static const uint8_t REPORT_IFEEL_BYTE     = 9;
    static const uint8_t REPORT_IFEEL_MASK     = 0b01000000;
    static const uint8_t REPORT_IFEEL_TEMP_BYTE = 24;

    /* byte 37 bit7 is set for ~6 s after the unit received a command from the IR remote */
    static const uint8_t REPORT_REMOTE_CMD_BYTE = 37;
    static const uint8_t REPORT_REMOTE_CMD_MASK = 0b10000000;
    static const uint8_t REPORT_DISP_MODE_AUTO     = 0;
    static const uint8_t REPORT_DISP_MODE_SET      = 1;
    static const uint8_t REPORT_DISP_MODE_ACT      = 2;
    static const uint8_t REPORT_DISP_MODE_OUT      = 3;

    static const uint8_t REPORT_DISP_F_BYTE    = 7;
    static const uint8_t REPORT_DISP_F_MASK    = 0b10000000;

    static const uint8_t REPORT_PLASMA1_BYTE   = 6;
    static const uint8_t REPORT_PLASMA1_MASK   = 0b00000100;
    static const uint8_t REPORT_PLASMA2_BYTE   = 0;
    static const uint8_t REPORT_PLASMA2_MASK   = 0b00000100;

    static const uint8_t REPORT_SLEEP_BYTE     = 4;
    static const uint8_t REPORT_SLEEP_MASK     = 0b00001000;

    static const uint8_t REPORT_XFAN_BYTE      = 6;
    static const uint8_t REPORT_XFAN_MASK      = 0b00001000;

    static const uint8_t REPORT_SAVE_BYTE      = 11;
    static const uint8_t REPORT_SAVE_MASK      = 0b01000000;

    /* SET packet shares all the byte definition with REPORT */
    static const uint8_t SET_PACKET_LEN        = 45;

    /* SET only: when set, the unit executes the command without beeping
       (flag found in the stock module firmware by gekkehenkie11); not reported back.
       Defaults for beeper_byte / beeper_mask in climate.py; some units ignore it. */
    static const uint8_t SET_NO_BEEP_BYTE      = 40;
    static const uint8_t SET_NO_BEEP_MASK      = 0b00000001;
    
    static const uint8_t SET_CONST_02_BYTE     = 39;
    static const uint8_t SET_CONST_02_VAL      = 0x02;

    static const uint8_t SET_AF_BYTE           = 3;
    static const uint8_t SET_AF_VAL            = 0xAF;

    static const uint8_t SET_NOCHANGE_BYTE     = 11;
    static const uint8_t SET_NOCHANGE_MASK     = 0b00001000;

    static const uint8_t SET_CONST_BIT_BYTE    = 7;
    static const uint8_t SET_CONST_BIT_MASK    = 0b00000010;

    /* time constraints */
    static const unsigned long TIME_REFRESH_PERIOD_MS   =  300;
    static const unsigned long TIME_TIMEOUT_INACTIVE_MS = 1000;

    /* Gree IR remote protocol, used to play the remote for I FEEL (the unit ignores I FEEL over UART).
       Command: header, 4 bytes LSB first, 3 footer bits 010, gap, 4 bytes, end mark.
       I FEEL temperature: own header, temperature in C, 0xA5, end mark. */
    static const uint32_t IR_CARRIER_HZ        = 38000;
    static const uint32_t IR_HDR_MARK          = 9000;
    static const uint32_t IR_HDR_SPACE         = 4500;
    static const uint32_t IR_BIT_MARK          = 620;
    static const uint32_t IR_ONE_SPACE         = 1600;
    static const uint32_t IR_ZERO_SPACE        = 540;
    static const uint32_t IR_MSG_SPACE         = 19980;
    /* I FEEL temperature frame: two header timings are in the wild, 6000/3000 (newer remotes,
       ESPHome gree YAC, HeatpumpIR YAP) and 8200/3800 (HeatpumpIR generic); the unit tells the
       temperature frame from a command by the header, so a wrong one is dropped as noise.
       Defaults here, overridable with i_feel_header_mark / i_feel_header_space. */
    static const uint32_t IR_IFEEL_HDR_MARK    = 6000;
    static const uint32_t IR_IFEEL_HDR_SPACE   = 3000;
    static const uint32_t IR_IFEEL_BIT_MARK    = 650;
    static const uint8_t  IR_IFEEL_TRAILER     = 0xA5;
    /* command byte 5: bits 0-1 display mode, bit 2 I FEEL, bits 3-5 constant 100 */
    static const uint8_t  IR_B5_CONST          = 0x20;
    static const uint8_t  IR_B5_IFEEL          = 0x04;

    /* I FEEL supervision */
    static const unsigned long I_FEEL_SETTLE_MS    = 1500;  /* wait after a change over UART before touching IR */
    static const unsigned long I_FEEL_NO_VALUE_MS  = 60000; /* warn when i_feel_sensor stays without a value */
    static const unsigned long I_FEEL_SENSOR_LOST_MS = 300000; /* sensor lost for this long: switch I FEEL off, back to the unit sensor */
    static const unsigned long I_FEEL_RETRY_MS     = 10000; /* between activation attempts */
    static const uint8_t       I_FEEL_MAX_ATTEMPTS = 3;
}

/* Define packets from AC that would be processed by software */
const std::vector<uint8_t> allowedPackets = {protocol::CMD_IN_UNIT_REPORT};

class SinclairACCNT : public SinclairAC {
    public:
        void control(const climate::ClimateCall &call) override;

        void on_horizontal_swing_change(const std::string &swing) override;
        void on_vertical_swing_change(const std::string &swing) override;

        void on_display_change(const std::string &display) override;
        void on_display_unit_change(const std::string &display_unit) override;

        void on_plasma_change(bool plasma) override;
        void on_beeper_change(bool beeper) override;
        void on_sleep_change(bool sleep) override;
        void on_xfan_change(bool xfan) override;
        void on_save_change(bool save) override;

        void setup() override;
        void loop() override;

    protected:
        ACState state_ = ACState::Initializing; /* Stores if the AC is responsive or not */
        ACUpdate update_ = ACUpdate::NoUpdate;  /* Stores if we need tu send update to AC or no */
        bool publish_pending_ = false;          /* Publish climate state on next handled report even if unchanged */
        bool ifeel_reported_ = false;           /* diagnostics: last I FEEL state / temperature / remote flag seen in reports */
        uint8_t ifeel_temp_reported_ = 0;
        bool remote_cmd_reported_ = false;

        /* I FEEL over IR */
        std::vector<uint8_t> last_report_;      /* payload of the last unit report, source of the IR command */
        uint32_t last_update_ms_ = 0;           /* last time a change over UART was in flight */
        uint32_t last_report_change_ms_ = 0;    /* last time the unit report differed from the previous one */
        uint32_t i_feel_last_cmd_ms_ = 0;       /* last IR command (I FEEL on/off) */
        uint32_t i_feel_last_temp_ms_ = 0;      /* last IR temperature frame */
        uint8_t i_feel_attempts_ = 0;           /* IR commands sent without the unit confirming the I FEEL state */
        bool i_feel_gave_up_ = false;
        bool i_feel_no_value_warned_ = false;
        bool i_feel_sensor_lost_ = false;       /* I FEEL switched off because the sensor value was lost */

        void i_feel_loop_();
        void ir_send_i_feel_command_(bool enable);
        void ir_send_i_feel_temperature_();
        void ir_append_command_(remote_base::RemoteTransmitData *data, bool i_feel);
        void ir_append_temperature_(remote_base::RemoteTransmitData *data);
        uint8_t i_feel_temperature_byte_();


        climate::ClimateMode mode_internal_;
        bool power_internal_;

        std::string display_mode_internal_;
        bool display_power_internal_;

        bool processUnitReport();

        void send_packet();

        bool verify_packet();
        void handle_packet();

        climate::ClimateMode determine_mode();
        const char* determine_fan_mode();

        std::string determine_vertical_swing();
        std::string determine_horizontal_swing();

        std::string determine_display();
        std::string determine_display_unit();

        bool determine_plasma();
        bool determine_sleep();
        bool determine_xfan();
        bool determine_save();
};

}  // namespace CNT
}  // namespace sinclair_ac
}  // namespace esphome
