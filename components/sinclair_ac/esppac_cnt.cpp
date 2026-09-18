// based on: https://github.com/DomiStyle/esphome-panasonic-ac
#include "esppac_cnt.h"

#include <cmath>

namespace esphome {
namespace sinclair_ac {
namespace CNT {

static const char *const TAG = "sinclair_ac.serial";

void SinclairACCNT::setup()
{
    SinclairAC::setup();

    ESP_LOGD(TAG, "Using serial protocol for Sinclair AC");
}

void SinclairACCNT::loop()
{
    /* this reads data from UART */
    SinclairAC::loop();

    /* we have a frame from AC */
    if (this->serialProcess_.state == STATE_COMPLETE)
    {
        /* do not forget to order for restart of the recieve state machine */
        this->serialProcess_.state = STATE_RESTART;
        /* log for ESPHome debug */
        log_packet(this->serialProcess_.data);

        if (!verify_packet())  /* Verify length, header, counter and checksum */
        {
            return;
        }

        this->last_packet_received_ = millis();  /* Set the time at which we received our last packet */

        /* A valid recieved packet of accepted type marks module as being ready */
        if (this->state_ != ACState::Ready)
        {
            this->state_ = ACState::Ready;  
            Component::status_clear_error();
            this->last_packet_sent_ = millis();
        }

        if (this->update_ == ACUpdate::NoUpdate)
        {
            handle_packet(); /* this will update state of components in HA as well as internal settings */
        }
    }


    /* I FEEL over IR: keep the unit regulating by the external sensor */
    if (this->update_ != ACUpdate::NoUpdate)
    {
        this->last_update_ms_ = millis();
    }
    this->i_feel_loop_();

    /* we will send a packet to the AC as a reponse to indicate changes */
    send_packet();

    /* if there are no packets for 5 seconds - mark module as not ready */
    if (millis() - this->last_packet_received_ >= protocol::TIME_TIMEOUT_INACTIVE_MS)
    {
        if (this->state_ != ACState::Initializing)
        {
            this->state_ = ACState::Initializing;
            Component::status_set_error();
        }
    }
}

/*
 * ESPHome control request
 */

void SinclairACCNT::control(const climate::ClimateCall &call)
{
    if (this->state_ != ACState::Ready)
        return;

    /* make sure HA gets the confirmed state once the unit has processed this request */
    this->publish_pending_ = true;

    if (call.get_mode().has_value())
    {
        ESP_LOGV(TAG, "Requested mode change");
        this->update_ = ACUpdate::UpdateStart;
        this->mode = *call.get_mode();
    }

    if (call.get_target_temperature().has_value())
    {
        ESP_LOGV(TAG, "Requested target teperature change");
        this->update_ = ACUpdate::UpdateStart;
        this->target_temperature = *call.get_target_temperature();
        if (this->target_temperature < MIN_TEMPERATURE)
        {
            this->target_temperature = MIN_TEMPERATURE;
        }
        else if (this->target_temperature > MAX_TEMPERATURE)
        {
            this->target_temperature = MAX_TEMPERATURE;
        }
    }

    if (call.has_custom_fan_mode())
    {
        ESP_LOGV(TAG, "Requested fan mode change");
        this->update_ = ACUpdate::UpdateStart;
        this->set_custom_fan_mode_(call.get_custom_fan_mode());
    }

    if (call.get_swing_mode().has_value())
    {
        ESP_LOGV(TAG, "Requested swing mode change");
        this->update_ = ACUpdate::UpdateStart;
        switch (*call.get_swing_mode()) {
            case climate::CLIMATE_SWING_BOTH:
                this->vertical_swing_state_   =   vertical_swing_options::FULL;
                this->horizontal_swing_state_ = horizontal_swing_options::FULL;
                break;
            case climate::CLIMATE_SWING_OFF:
                /* both center */
                this->vertical_swing_state_   =   vertical_swing_options::CMID;
                this->horizontal_swing_state_ = horizontal_swing_options::CMID;
                break;
            case climate::CLIMATE_SWING_VERTICAL:
                /* vertical full, horizontal center */
                this->vertical_swing_state_   =   vertical_swing_options::FULL;
                this->horizontal_swing_state_ = horizontal_swing_options::CMID;
                break;
            case climate::CLIMATE_SWING_HORIZONTAL:
                /* horizontal full, vertical center */
                this->vertical_swing_state_   =   vertical_swing_options::CMID;
                this->horizontal_swing_state_ = horizontal_swing_options::FULL;
                break;
            default:
                ESP_LOGV(TAG, "Unsupported swing mode requested");
                /* both center */
                this->vertical_swing_state_   =   vertical_swing_options::CMID;
                this->horizontal_swing_state_ = horizontal_swing_options::CMID;
                break;
        }
        if (!this->horizontal_swing_)
        {
            /* unit has no horizontal louvers, do not ask for any position */
            this->horizontal_swing_state_ = horizontal_swing_options::OFF;
        }
    }
}

/*
 * Send a raw packet, as is
 */

void SinclairACCNT::send_packet()
{
    std::vector<uint8_t> packet(protocol::SET_PACKET_LEN, 0);  /* Initialize packet contents */

    /* The stock Gree WiFi module transmits on a fixed timer: one packet every
       TIME_REFRESH_PERIOD_MS, regardless of when the unit report arrived.
       Sending earlier (previously: right after each received report) makes
       some units drop the command with 0xAF, see issues #2 and #25. */
    if ((millis() - this->last_packet_sent_) < protocol::TIME_REFRESH_PERIOD_MS)
    {
        return;
    }
    
    packet[protocol::SET_CONST_02_BYTE] = protocol::SET_CONST_02_VAL; /* Some always 0x02 byte... */
    packet[protocol::SET_CONST_BIT_BYTE] = protocol::SET_CONST_BIT_MASK; /* Some always true bit */

    /* Prepare the rest of the frame */
    /* this handles tricky part of 0xAF value and flag marking that WiFi does not apply any changes */
    switch(this->update_)
    {
        default:
        case ACUpdate::NoUpdate:
            packet[protocol::SET_NOCHANGE_BYTE] |= protocol::SET_NOCHANGE_MASK;
            break;
        case ACUpdate::UpdateStart:
            packet[protocol::SET_AF_BYTE] = protocol::SET_AF_VAL;
            break;
        case ACUpdate::UpdateClear:
            break;
    }

    /* MODE and POWER --------------------------------------------------------------------------- */
    uint8_t mode = protocol::REPORT_MODE_AUTO;
    bool power = false;
    switch (this->mode)
    {
        case climate::CLIMATE_MODE_AUTO:
            mode = protocol::REPORT_MODE_AUTO;
            power = true;
            break;
        case climate::CLIMATE_MODE_COOL:
            mode = protocol::REPORT_MODE_COOL;
            power = true;
            break;
        case climate::CLIMATE_MODE_DRY:
            mode = protocol::REPORT_MODE_DRY;
            power = true;
            break;
        case climate::CLIMATE_MODE_FAN_ONLY:
            mode = protocol::REPORT_MODE_FAN;
            power = true;
            break;
        case climate::CLIMATE_MODE_HEAT:
            mode = protocol::REPORT_MODE_HEAT;
            power = true;
            break;
        default:
        case climate::CLIMATE_MODE_OFF:
            /* In case of MODE_OFF we will not alter the last mode setting recieved from AC, see determine_mode() */
            switch (this->mode_internal_)
            {
                case climate::CLIMATE_MODE_AUTO:
                    mode = protocol::REPORT_MODE_AUTO;
                    break;
                case climate::CLIMATE_MODE_COOL:
                    mode = protocol::REPORT_MODE_COOL;
                    break;
                case climate::CLIMATE_MODE_DRY:
                    mode = protocol::REPORT_MODE_DRY;
                    break;
                case climate::CLIMATE_MODE_FAN_ONLY:
                    mode = protocol::REPORT_MODE_FAN;
                    break;
                case climate::CLIMATE_MODE_HEAT:
                    mode = protocol::REPORT_MODE_HEAT;
                    break;
                default:
                    /* OFF / HEAT_COOL never come from determine_mode(), keep AUTO */
                    break;
            }
            power = false;
            break;
    }

    packet[protocol::REPORT_MODE_BYTE] |= (mode << protocol::REPORT_MODE_POS);
    if (power)
    {
        packet[protocol::REPORT_PWR_BYTE] |= protocol::REPORT_PWR_MASK;
    }

    /* TARGET TEMPERATURE --------------------------------------------------------------------------- */
    uint8_t target_temperature = ((((uint8_t)this->target_temperature) - protocol::REPORT_TEMP_SET_OFF) << protocol::REPORT_TEMP_SET_POS);
    packet[protocol::REPORT_TEMP_SET_BYTE] |= (target_temperature & protocol::REPORT_TEMP_SET_MASK);

    /* FAN SPEED --------------------------------------------------------------------------- */
    /* below will default to AUTO */
    uint8_t fanSpeed1 = 0;
    uint8_t fanSpeed2 = 0;
    bool    fanQuiet  = false;
    bool    fanTurbo  = false;
    if (this->has_custom_fan_mode())
    {
        switch (this->fan_mode_from_label(this->get_custom_fan_mode().c_str()))
        {
            case FanMode::Low:   fanSpeed1 = 1; fanSpeed2 = 1; break;
            case FanMode::Quiet: fanSpeed1 = 1; fanSpeed2 = 1; fanQuiet = true; break;
            case FanMode::MedLow:  fanSpeed1 = 2; fanSpeed2 = 2; break;
            case FanMode::Med:   fanSpeed1 = 3; fanSpeed2 = 2; break;
            case FanMode::MedHigh:  fanSpeed1 = 4; fanSpeed2 = 3; break;
            case FanMode::High:  fanSpeed1 = 5; fanSpeed2 = 3; break;
            case FanMode::Turbo: fanSpeed1 = 5; fanSpeed2 = 3; fanTurbo = true; break;
            case FanMode::Auto:
            default:             break;
        }
    }

    /* 3-speed units use only fanSpeed2 (1..3) and do not know the fine 5-level field */
    if (this->fan_speeds_ == 3)
    {
        fanSpeed1 = 0;
    }

    packet[protocol::REPORT_FAN_SPD1_BYTE] |= (fanSpeed1 << protocol::REPORT_FAN_SPD1_POS);
    packet[protocol::REPORT_FAN_SPD2_BYTE] |= (fanSpeed2 << protocol::REPORT_FAN_SPD2_POS);
    if (fanTurbo)
    {
        packet[protocol::REPORT_FAN_TURBO_BYTE] |= protocol::REPORT_FAN_TURBO_MASK;
    }
    if (fanQuiet)
    {
        packet[protocol::REPORT_FAN_QUIET_BYTE] |= protocol::REPORT_FAN_QUIET_MASK;
    }

    /* VERTICAL SWING --------------------------------------------------------------------------- */
    uint8_t mode_vertical_swing = protocol::REPORT_VSWING_OFF;
    if (this->vertical_swing_state_ == vertical_swing_options::OFF)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_OFF;
    }
    else if (this->vertical_swing_state_ == vertical_swing_options::FULL)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_FULL;
    }
    else if (this->vertical_swing_state_ == vertical_swing_options::DOWN)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_DOWN;
    }
    else if (this->vertical_swing_state_ == vertical_swing_options::MIDD)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_MIDD;
    }
    else if (this->vertical_swing_state_ == vertical_swing_options::MID)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_MID;
    }
    else if (this->vertical_swing_state_ == vertical_swing_options::MIDU)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_MIDU;
    }
    else if (this->vertical_swing_state_ == vertical_swing_options::UP)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_UP;
    }
    else if (this->vertical_swing_state_ == vertical_swing_options::CDOWN)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_CDOWN;
    }
    else if (this->vertical_swing_state_ == vertical_swing_options::CMIDD)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_CMIDD;
    }
    else if (this->vertical_swing_state_ == vertical_swing_options::CMID)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_CMID;
    }
    else if (this->vertical_swing_state_ == vertical_swing_options::CMIDU)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_CMIDU;
    }
    else if (this->vertical_swing_state_ == vertical_swing_options::CUP)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_CUP;
    }
    else
    {
        mode_vertical_swing = protocol::REPORT_VSWING_OFF;
    }
    packet[protocol::REPORT_VSWING_BYTE] |= (mode_vertical_swing << protocol::REPORT_VSWING_POS);

    /* HORIZONTAL SWING --------------------------------------------------------------------------- */
    uint8_t mode_horizontal_swing = protocol::REPORT_HSWING_OFF;
    if (this->horizontal_swing_state_ == horizontal_swing_options::OFF)
    {
        mode_horizontal_swing = protocol::REPORT_HSWING_OFF;
    }
    else if (this->horizontal_swing_state_ == horizontal_swing_options::FULL)
    {
        mode_horizontal_swing = protocol::REPORT_HSWING_FULL;
    }
    else if (this->horizontal_swing_state_ == horizontal_swing_options::CLEFT)
    {
        mode_horizontal_swing = protocol::REPORT_HSWING_CLEFT;
    }
    else if (this->horizontal_swing_state_ == horizontal_swing_options::CMIDL)
    {
        mode_horizontal_swing = protocol::REPORT_HSWING_CMIDL;
    }
    else if (this->horizontal_swing_state_ == horizontal_swing_options::CMID)
    {
        mode_horizontal_swing = protocol::REPORT_HSWING_CMID;
    }
    else if (this->horizontal_swing_state_ == horizontal_swing_options::CMIDR)
    {
        mode_horizontal_swing = protocol::REPORT_HSWING_CMIDR;
    }
    else if (this->horizontal_swing_state_ == horizontal_swing_options::CRIGHT)
    {
        mode_horizontal_swing = protocol::REPORT_HSWING_CRIGHT;
    }
    else
    {
        mode_horizontal_swing = protocol::REPORT_HSWING_OFF;
    }
    packet[protocol::REPORT_HSWING_BYTE] |= (mode_horizontal_swing << protocol::REPORT_HSWING_POS);

    /* DISPLAY --------------------------------------------------------------------------- */
    uint8_t display_mode = protocol::REPORT_DISP_MODE_AUTO;
    if (this->display_state_ == display_options::AUTO)
    {
        display_mode = protocol::REPORT_DISP_MODE_AUTO;
        this->display_power_internal_ = true;
    }
    else if (this->display_state_ == display_options::SET)
    {
        display_mode = protocol::REPORT_DISP_MODE_SET;
        this->display_power_internal_ = true;
    }
    else if (this->display_state_ == display_options::ACT)
    {
        display_mode = protocol::REPORT_DISP_MODE_ACT;
        this->display_power_internal_ = true;
    }
    else if (this->display_state_ == display_options::OUT)
    {
        display_mode = protocol::REPORT_DISP_MODE_OUT;
        this->display_power_internal_ = true;
    }
    else if (this->display_state_ == display_options::OFF)
    {
        /* we do not want to alter display setting - only turn it off */
        this->display_power_internal_ = false;
        if (this->display_mode_internal_ == display_options::AUTO)
        {
            display_mode = protocol::REPORT_DISP_MODE_AUTO;
        }
        else if (this->display_mode_internal_ == display_options::SET)
        {
            display_mode = protocol::REPORT_DISP_MODE_SET;
        }
        else if (this->display_mode_internal_ == display_options::ACT)
        {
            display_mode = protocol::REPORT_DISP_MODE_ACT;
        }
        else if (this->display_mode_internal_ == display_options::OUT)
        {
            display_mode = protocol::REPORT_DISP_MODE_OUT;
        }
        else
        {
            display_mode = protocol::REPORT_DISP_MODE_AUTO;
        }
    }
    else
    {
        display_mode = protocol::REPORT_DISP_MODE_AUTO;
        this->display_power_internal_ = true;
    }

    packet[protocol::REPORT_DISP_MODE_BYTE] |= (display_mode << protocol::REPORT_DISP_MODE_POS);

    if (this->display_power_internal_)
    {
        packet[protocol::REPORT_DISP_ON_BYTE] |= protocol::REPORT_DISP_ON_MASK;
    }

    /* DISPLAY UNIT --------------------------------------------------------------------------- */
    if (this->display_unit_state_ == display_unit_options::DEGF)
    {
        packet[protocol::REPORT_DISP_F_BYTE] |= protocol::REPORT_DISP_F_MASK;
    }

    /* PLASMA --------------------------------------------------------------------------- */
    if (this->plasma_state_)
    {
        packet[protocol::REPORT_PLASMA1_BYTE] |= protocol::REPORT_PLASMA1_MASK;
        packet[protocol::REPORT_PLASMA2_BYTE] |= protocol::REPORT_PLASMA2_MASK;
    }


    /* BEEPER --------------------------------------------------------------------------- */
    if (!this->beeper_state_)
    {
        packet[this->beeper_byte_] |= this->beeper_mask_;
    }

    /* SLEEP --------------------------------------------------------------------------- */
    if (this->sleep_state_)
    {
        packet[protocol::REPORT_SLEEP_BYTE] |= protocol::REPORT_SLEEP_MASK;
    }

    /* XFAN --------------------------------------------------------------------------- */
    if (this->xfan_state_)
    {
        packet[protocol::REPORT_XFAN_BYTE] |= protocol::REPORT_XFAN_MASK;
    }

    /* SAVE --------------------------------------------------------------------------- */
    if (this->save_state_)
    {
        packet[protocol::REPORT_SAVE_BYTE] |= protocol::REPORT_SAVE_MASK;
    }
    
    /* Do the command, length */
    packet.insert(packet.begin(), protocol::CMD_OUT_PARAMS_SET);
    packet.insert(packet.begin(), protocol::SET_PACKET_LEN + 2); /* Add 2 bytes as we added a command and will add checksum */

    /* Do checksum - sum of all bytes except sync and checksum itself% 0x100 
       the module would be realized by the fact that we are using uint8_t*/
    uint8_t checksum = 0;
    for (uint8_t i = 0 ; i < packet.size() ; i++)
    {
        checksum += packet[i];
    }
    packet.push_back(checksum);

    /* Do SYNC bytes */
    packet.insert(packet.begin(), protocol::SYNC);
    packet.insert(packet.begin(), protocol::SYNC);

    this->last_packet_sent_ = millis();  /* Save the time when we sent the last packet */
    write_array(packet);                 /* Sent the packet by UART */
    log_packet(packet, true);            /* Log uart for debug purposes */

    /* update setting state-machine */
    switch(this->update_)
    {
        case ACUpdate::NoUpdate:
            break;
        case ACUpdate::UpdateStart:
            this->update_ = ACUpdate::UpdateClear;
            break;
        case ACUpdate::UpdateClear:
            this->update_ = ACUpdate::NoUpdate;
            break;
        default:
            this->update_ = ACUpdate::NoUpdate;
            break;
    }
}

/*
 * I FEEL over IR
 *
 * The unit takes the room temperature from outside only through its IR receiver (tested: the
 * same fields in UART SET packets are ignored). With ir_transmitter_id the component plays the
 * remote: a full Gree command with the I FEEL bit switches the function on, then short
 * temperature frames keep it fed, like the remote does every 10 minutes. The command is built
 * from the last unit report, so it changes nothing but the I FEEL state.
 */

static void ir_append_bits(remote_base::RemoteTransmitData *data, uint8_t value, uint8_t bits, uint32_t bit_mark)
{
    for (uint8_t i = 0; i < bits; i++)
    {
        data->mark(bit_mark);
        data->space((value & 0x01) ? protocol::IR_ONE_SPACE : protocol::IR_ZERO_SPACE);
        value >>= 1;
    }
}

void SinclairACCNT::ir_append_command_(remote_base::RemoteTransmitData *data, bool i_feel)
{
    const std::vector<uint8_t> &r = this->last_report_;

    bool    power  = (r[protocol::REPORT_PWR_BYTE] & protocol::REPORT_PWR_MASK) != 0;
    uint8_t mode   = (r[protocol::REPORT_MODE_BYTE] & protocol::REPORT_MODE_MASK) >> protocol::REPORT_MODE_POS;
    uint8_t fan    = (r[protocol::REPORT_FAN_SPD2_BYTE] & protocol::REPORT_FAN_SPD2_MASK) >> protocol::REPORT_FAN_SPD2_POS;
    bool    sleep  = (r[protocol::REPORT_SLEEP_BYTE] & protocol::REPORT_SLEEP_MASK) != 0;
    uint8_t temp   = (r[protocol::REPORT_TEMP_SET_BYTE] & protocol::REPORT_TEMP_SET_MASK) >> protocol::REPORT_TEMP_SET_POS;
    bool    turbo  = (r[protocol::REPORT_FAN_TURBO_BYTE] & protocol::REPORT_FAN_TURBO_MASK) != 0;
    bool    light  = (r[protocol::REPORT_DISP_ON_BYTE] & protocol::REPORT_DISP_ON_MASK) != 0;
    bool    health = (r[protocol::REPORT_PLASMA1_BYTE] & protocol::REPORT_PLASMA1_MASK) != 0;
    bool    xfan   = (r[protocol::REPORT_XFAN_BYTE] & protocol::REPORT_XFAN_MASK) != 0;
    bool    use_f  = (r[protocol::REPORT_DISP_F_BYTE] & protocol::REPORT_DISP_F_MASK) != 0;
    bool    half_f = (r[protocol::REPORT_DISP_F_BYTE] & 0x40) != 0;
    uint8_t vswing = (r[protocol::REPORT_VSWING_BYTE] & protocol::REPORT_VSWING_MASK) >> protocol::REPORT_VSWING_POS;
    uint8_t hswing = (r[protocol::REPORT_HSWING_BYTE] & protocol::REPORT_HSWING_MASK) >> protocol::REPORT_HSWING_POS;
    uint8_t disp   = (r[protocol::REPORT_DISP_MODE_BYTE] & protocol::REPORT_DISP_MODE_MASK) >> protocol::REPORT_DISP_MODE_POS;
    bool    save   = (r[protocol::REPORT_SAVE_BYTE] & protocol::REPORT_SAVE_MASK) != 0;

    /* louver position codes are the same on UART and IR; the IR frame also flags the swinging ones */
    bool swing_auto = (vswing == protocol::REPORT_VSWING_FULL) || (vswing >= protocol::REPORT_VSWING_DOWN);

    uint8_t b[8];
    b[0] = mode | (power ? 0x08 : 0) | (fan << 4) | (swing_auto ? 0x40 : 0) | (sleep ? 0x80 : 0);
    b[1] = temp;
    b[2] = (turbo ? 0x10 : 0) | (light ? 0x20 : 0) | (health ? 0x40 : 0) | (xfan ? 0x80 : 0);
    b[3] = 0x50 | (use_f ? 0x08 : 0) | (half_f ? 0x04 : 0);
    b[4] = vswing | (hswing << 4);
    b[5] = disp | protocol::IR_B5_CONST | (i_feel ? protocol::IR_B5_IFEEL : 0);
    b[6] = 0x00;
    b[7] = (save ? 0x04 : 0);
    uint8_t sum = ((b[0] & 0x0F) + (b[1] & 0x0F) + (b[2] & 0x0F) + (b[3] & 0x0F) +
                   (b[4] >> 4) + (b[5] >> 4) + (b[6] >> 4) + 0x0A) & 0x0F;
    b[7] |= (sum << 4);

    ESP_LOGD(TAG, "IR command %02X %02X %02X %02X %02X %02X %02X %02X (I FEEL %s)",
             b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], i_feel ? "on" : "off");

    data->mark(protocol::IR_HDR_MARK);
    data->space(protocol::IR_HDR_SPACE);
    for (uint8_t i = 0; i < 4; i++)
        ir_append_bits(data, b[i], 8, protocol::IR_BIT_MARK);
    ir_append_bits(data, 0b010, 3, protocol::IR_BIT_MARK);
    data->mark(protocol::IR_BIT_MARK);
    data->space(protocol::IR_MSG_SPACE);
    for (uint8_t i = 4; i < 8; i++)
        ir_append_bits(data, b[i], 8, protocol::IR_BIT_MARK);
    data->mark(protocol::IR_BIT_MARK);
}

uint8_t SinclairACCNT::i_feel_temperature_byte_()
{
    /* whole degrees C, as the remote sends them */
    float t = this->i_feel_temperature_;
    if (std::isnan(t)) return 0;
    if (t < 0.0f) t = 0.0f;
    if (t > 60.0f) t = 60.0f;
    return (uint8_t) std::lround(t);
}

void SinclairACCNT::ir_append_temperature_(remote_base::RemoteTransmitData *data)
{
    uint8_t temperature = this->i_feel_temperature_byte_();

    data->mark(this->i_feel_header_mark_);
    data->space(this->i_feel_header_space_);
    ir_append_bits(data, temperature, 8, protocol::IR_IFEEL_BIT_MARK);
    ir_append_bits(data, protocol::IR_IFEEL_TRAILER, 8, protocol::IR_IFEEL_BIT_MARK);
    data->mark(protocol::IR_IFEEL_BIT_MARK);
}

void SinclairACCNT::ir_send_i_feel_command_(bool enable)
{
    auto call = this->ir_transmitter_->transmit();
    auto *data = call.get_data();
    data->set_carrier_frequency(protocol::IR_CARRIER_HZ);
    data->reserve(150);
    this->ir_append_command_(data, enable);
    call.set_send_times(1);
    call.perform();
    /* the temperature follows as a frame of its own once the unit reports I FEEL active,
       see i_feel_loop_() */
}

void SinclairACCNT::ir_send_i_feel_temperature_()
{
    ESP_LOGD(TAG, "IR I FEEL temperature %.0f C (header %u/%u us)", this->i_feel_temperature_,
             (unsigned) this->i_feel_header_mark_, (unsigned) this->i_feel_header_space_);

    auto call = this->ir_transmitter_->transmit();
    auto *data = call.get_data();
    data->set_carrier_frequency(protocol::IR_CARRIER_HZ);
    data->reserve(40);
    this->ir_append_temperature_(data);
    call.set_send_times(1);
    call.perform();
}

void SinclairACCNT::i_feel_loop_()
{
    if (this->ir_transmitter_ == nullptr || this->i_feel_sensor_ == nullptr)
        return;
    if (this->state_ != ACState::Ready || this->update_ != ACUpdate::NoUpdate)
        return;
    if (this->last_report_.size() < protocol::SET_PACKET_LEN)
        return;

    uint32_t now = millis();

    /* a change over UART is settling: reports may still show the old state */
    if ((now - this->last_update_ms_) < protocol::I_FEEL_SETTLE_MS)
        return;

    /* the unit drops I FEEL when powered off; start over at the next power on */
    bool power = (this->last_report_[protocol::REPORT_PWR_BYTE] & protocol::REPORT_PWR_MASK) != 0;
    if (!power)
    {
        this->i_feel_attempts_ = 0;
        this->i_feel_gave_up_ = false;
        return;
    }

    /* I FEEL is wanted but the sensor has no value yet (e.g. right after boot, before Home
       Assistant connected): leave the unit as it is, do not switch a running I FEEL off */
    if (this->i_feel_enabled_ && std::isnan(this->i_feel_temperature_))
    {
        if (!this->i_feel_no_value_warned_ && now >= protocol::I_FEEL_NO_VALUE_MS)
        {
            ESP_LOGW(TAG, "i_feel_sensor has no value, I FEEL is not managed (check the sensor / entity id)");
            this->i_feel_no_value_warned_ = true;
        }
        return;
    }
    this->i_feel_no_value_warned_ = false;

    bool want = this->i_feel_enabled_;
    bool active = this->ifeel_reported_;

    if (want == active)
    {
        this->i_feel_attempts_ = 0;
        this->i_feel_gave_up_ = false;
        if (active && (this->i_feel_temp_dirty_ || (now - this->i_feel_last_temp_ms_) >= this->i_feel_interval_ms_))
        {
            this->i_feel_temp_dirty_ = false;
            this->i_feel_last_temp_ms_ = now;
            this->ir_send_i_feel_temperature_();
        }
        return;
    }

    /* the I FEEL state of the unit differs from the wanted one: send the command, a few times
       at most, because every command makes the unit beep */
    if (this->i_feel_gave_up_)
        return;
    if (this->i_feel_attempts_ > 0 && (now - this->i_feel_last_cmd_ms_) < protocol::I_FEEL_RETRY_MS)
        return;
    if (this->i_feel_attempts_ >= protocol::I_FEEL_MAX_ATTEMPTS)
    {
        ESP_LOGW(TAG, "Unit did not switch I FEEL %s after %u IR commands, giving up until the next power on",
                 want ? "on" : "off", this->i_feel_attempts_);
        this->i_feel_gave_up_ = true;
        return;
    }

    this->i_feel_attempts_++;
    this->i_feel_last_cmd_ms_ = now;
    /* send the temperature as soon as the unit confirms I FEEL */
    this->i_feel_temp_dirty_ = true;
    this->ir_send_i_feel_command_(want);
}

/*
 * Packet handling
 */

bool SinclairACCNT::verify_packet()
{
    /* At least 2 sync bytes + length + type + checksum */
    if (this->serialProcess_.data.size() < 5)
    {
        ESP_LOGW(TAG, "Dropping invalid packet (length)");
        return false;
    }

    /* The header (aka sync bytes) was checked by SinclairAC::read_data() */

    /* The frame len was assumed by SinclairAC::read_data() */

    /* Check if this packet type sould be processed */
    bool commandAllowed = false;
    for (uint8_t packet : allowedPackets)
    {
        if (this->serialProcess_.data[3] == packet)
        {
            commandAllowed = true;
            break;
        }
    }
    if (!commandAllowed)
    {
        ESP_LOGW(TAG, "Dropping invalid packet (command [%02X] not allowed)", this->serialProcess_.data[3]);
        return false;
    }

    /* Check checksum - sum of all bytes except sync and checksum itself% 0x100 
       the module would be realized by the fact that we are using uint8_t*/
    uint8_t checksum = 0;
    for (uint8_t i = 2 ; i < this->serialProcess_.data.size() - 1 ; i++)
    {
        checksum += this->serialProcess_.data[i];
    }
    if (checksum != this->serialProcess_.data[this->serialProcess_.data.size()-1])
    {
        ESP_LOGD(TAG, "Dropping invalid packet (checksum)");
        return false;
    }

    return true;
}

void SinclairACCNT::handle_packet()
{
    if (this->serialProcess_.data[3] == protocol::CMD_IN_UNIT_REPORT)
    {
        /* here we will remove unnecessary elements - header and checksum */
        this->serialProcess_.data.erase(this->serialProcess_.data.begin(), this->serialProcess_.data.begin() + 4); /* remove header */
        this->serialProcess_.data.pop_back();  /* remove checksum */
        /* now process the data */
        bool changed = this->processUnitReport();

        /* keep the raw payload, the IR command for I FEEL is derived from it bit by bit */
        this->last_report_ = this->serialProcess_.data;

        /* diagnostics: I FEEL state and temperature received from the IR remote, and IR command flag */
        bool ifeel = (this->serialProcess_.data[protocol::REPORT_IFEEL_BYTE] & protocol::REPORT_IFEEL_MASK) != 0;
        uint8_t ifeelTemp = this->serialProcess_.data[protocol::REPORT_IFEEL_TEMP_BYTE];
        bool remoteCmd = (this->serialProcess_.data[protocol::REPORT_REMOTE_CMD_BYTE] & protocol::REPORT_REMOTE_CMD_MASK) != 0;
        if (ifeel != this->ifeel_reported_ || ifeelTemp != this->ifeel_temp_reported_)
        {
            ESP_LOGD(TAG, "Unit reports I FEEL %s, I FEEL temperature %u C", ifeel ? "active" : "inactive", ifeelTemp);
            this->ifeel_reported_ = ifeel;
            this->ifeel_temp_reported_ = ifeelTemp;
        }
        if (remoteCmd != this->remote_cmd_reported_)
        {
            if (remoteCmd)
                ESP_LOGD(TAG, "Unit received a command from the IR remote");
            this->remote_cmd_reported_ = remoteCmd;
        }
        /* Reports arrive every ~300 ms; publishing each one floods Home Assistant
           and makes a value just changed in HA flip back before the unit confirms it.
           Publish only when something changed, or once after a request from HA so
           HA sees the confirmed state even if it equals what it asked for. */
        if (changed || this->publish_pending_)
        {
            this->publish_pending_ = false;
            this->publish_state();
        }
    }
    else 
    {
        ESP_LOGD(TAG, "Received unknown packet");
    }
}

/*
 * This decodes frame recieved from AC Unit
 */
bool SinclairACCNT::processUnitReport()
{
    bool hasChanged = false;

    climate::ClimateMode newMode = determine_mode();
    if (this->mode != newMode) hasChanged = true;
    this->mode = newMode;

    const char* newFanMode = determine_fan_mode();
    if (this->has_custom_fan_mode())
    {
        if (strcmp(this->get_custom_fan_mode().c_str(), newFanMode) != 0) hasChanged = true;
    }
    else
    {
        hasChanged = true;
    }
    this->set_custom_fan_mode_(newFanMode);
    
    float newTargetTemperature = (float)(((this->serialProcess_.data[protocol::REPORT_TEMP_SET_BYTE] & protocol::REPORT_TEMP_SET_MASK) >> protocol::REPORT_TEMP_SET_POS)
        + protocol::REPORT_TEMP_SET_OFF);
    if (this->target_temperature != newTargetTemperature) hasChanged = true;
    this->update_target_temperature(newTargetTemperature);
    
    /* if there is no external sensor mapped to represent current temperature we will get data from AC unit */
    if (this->current_temperature_sensor_ == nullptr)
    {
        uint8_t rawCurrentTemperature = (this->serialProcess_.data[protocol::REPORT_TEMP_ACT_BYTE] & protocol::REPORT_TEMP_ACT_MASK) >> protocol::REPORT_TEMP_ACT_POS;
        float newCurrentTemperature;
        if (this->current_temperature_gree_)
        {
            /* Gree-based units (Coolexpert ACH-09BI, sniffed stock module: 0x36 -> 14 C, 0x38 -> 16 C) */
            newCurrentTemperature = (float) rawCurrentTemperature - protocol::REPORT_TEMP_ACT_OFF_GREE;
        }
        else
        {
            /* Sinclair MV-H09BIF */
            newCurrentTemperature = ((float) rawCurrentTemperature - protocol::REPORT_TEMP_ACT_OFF) / protocol::REPORT_TEMP_ACT_DIV;
        }
        if (this->current_temperature != newCurrentTemperature) hasChanged = true;
        this->update_current_temperature(newCurrentTemperature);
    }

    std::string verticalSwing = determine_vertical_swing();
    std::string horizontalSwing = determine_horizontal_swing();

    this->update_swing_vertical(verticalSwing);
    this->update_swing_horizontal(horizontalSwing);

    climate::ClimateSwingMode newSwingMode;
    /* update legacy swing mode to somehow represent actual state and support
       this setting without detailed settings done with additional switches */
    if (verticalSwing == vertical_swing_options::FULL && horizontalSwing == horizontal_swing_options::FULL)
        newSwingMode = climate::CLIMATE_SWING_BOTH;
    else if (verticalSwing == vertical_swing_options::FULL)
        newSwingMode = climate::CLIMATE_SWING_VERTICAL;
    else if (horizontalSwing == horizontal_swing_options::FULL)
        newSwingMode = climate::CLIMATE_SWING_HORIZONTAL;
    else
        newSwingMode = climate::CLIMATE_SWING_OFF;
    
    if (this->swing_mode != newSwingMode) hasChanged = true;
    this->swing_mode = newSwingMode;

    this->update_display(determine_display());
    this->update_display_unit(determine_display_unit());

    this->update_plasma(determine_plasma());
    this->update_sleep(determine_sleep());
    this->update_xfan(determine_xfan());
    this->update_save(determine_save());

    return hasChanged;
}

climate::ClimateMode SinclairACCNT::determine_mode()
{
    uint8_t mode = (this->serialProcess_.data[protocol::REPORT_MODE_BYTE] & protocol::REPORT_MODE_MASK) >> protocol::REPORT_MODE_POS;

    /* as mode presented by climate component incorporates both power and mode we will store this separately for Sinclair
       in _internal_ fields */
    /* check unit power flag */
    this->power_internal_ = (this->serialProcess_.data[protocol::REPORT_PWR_BYTE] & protocol::REPORT_PWR_MASK) != 0;

    /* check unit mode */
    switch (mode)
    {
        case protocol::REPORT_MODE_AUTO:
            this->mode_internal_ = climate::CLIMATE_MODE_AUTO;
            break;
        case protocol::REPORT_MODE_COOL:
            this->mode_internal_ = climate::CLIMATE_MODE_COOL;
            break;
        case protocol::REPORT_MODE_DRY:
            this->mode_internal_ = climate::CLIMATE_MODE_DRY;
            break;
        case protocol::REPORT_MODE_FAN:
            this->mode_internal_ = climate::CLIMATE_MODE_FAN_ONLY;
            break;
        case protocol::REPORT_MODE_HEAT:
            this->mode_internal_ = climate::CLIMATE_MODE_HEAT;
            break;
        default:
            ESP_LOGW(TAG, "Received unknown climate mode");
            this->mode_internal_ = climate::CLIMATE_MODE_OFF;
            break;
    }

    /* if unit is powered on - return the mode, otherwise return CLIMATE_MODE_OFF */
    if (this->power_internal_)
    {
        return this->mode_internal_;
    }
    else
    {
        return climate::CLIMATE_MODE_OFF;
    }
}

const char* SinclairACCNT::determine_fan_mode()
{
    /* fan setting has quite complex representation in the packet, brace for it */
    uint8_t fanSpeed1 = (this->serialProcess_.data[protocol::REPORT_FAN_SPD1_BYTE]  & protocol::REPORT_FAN_SPD1_MASK) >> protocol::REPORT_FAN_SPD1_POS;
    uint8_t fanSpeed2 = (this->serialProcess_.data[protocol::REPORT_FAN_SPD2_BYTE]  & protocol::REPORT_FAN_SPD2_MASK) >> protocol::REPORT_FAN_SPD2_POS;
    bool    fanQuiet  = (this->serialProcess_.data[protocol::REPORT_FAN_QUIET_BYTE] & protocol::REPORT_FAN_QUIET_MASK) != 0;
    bool    fanTurbo  = (this->serialProcess_.data[protocol::REPORT_FAN_TURBO_BYTE] & protocol::REPORT_FAN_TURBO_MASK) != 0;

    if (fanTurbo)
    {
        return this->fan_mode_label(FanMode::Turbo);
    }
    if (fanQuiet)
    {
        return this->fan_mode_label(FanMode::Quiet);
    }

    /* 5-speed units (Sinclair MV-H09BIF) report the fine speed in fanSpeed1 (1..5)
       together with the coarse one in fanSpeed2 (1..3). 3-speed units (e.g. Coolexpert
       ACH-09BI, Gree 3-speed models) report only fanSpeed2 and keep fanSpeed1 at 0;
       some units (Lennox, see issue #1) put garbage there. Use fanSpeed1 only when it
       is plausible, otherwise fall back to fanSpeed2. */
    if (this->fan_speeds_ == 5)
    {
        switch (fanSpeed1)
        {
            case 1: return this->fan_mode_label(FanMode::Low);
            case 2: return this->fan_mode_label(FanMode::MedLow);
            case 3: return this->fan_mode_label(FanMode::Med);
            case 4: return this->fan_mode_label(FanMode::MedHigh);
            case 5: return this->fan_mode_label(FanMode::High);
            default: break;
        }
    }

    switch (fanSpeed2)
    {
        case 0: return this->fan_mode_label(FanMode::Auto);
        case 1: return this->fan_mode_label(FanMode::Low);
        case 2: return this->fan_mode_label(FanMode::Med);
        case 3: return this->fan_mode_label(FanMode::High);
        default: break;
    }

    ESP_LOGW(TAG, "Received unknown fan mode (fanSpeed1=%u fanSpeed2=%u)", fanSpeed1, fanSpeed2);
    return this->fan_mode_label(FanMode::Auto);
}

std::string SinclairACCNT::determine_vertical_swing()
{
    uint8_t mode = (this->serialProcess_.data[protocol::REPORT_VSWING_BYTE]  & protocol::REPORT_VSWING_MASK) >> protocol::REPORT_VSWING_POS;

    switch (mode) {
        case protocol::REPORT_VSWING_OFF:
            return vertical_swing_options::OFF;
        case protocol::REPORT_VSWING_FULL:
            return vertical_swing_options::FULL;
        case protocol::REPORT_VSWING_DOWN:
            return vertical_swing_options::DOWN;
        case protocol::REPORT_VSWING_MIDD:
            return vertical_swing_options::MIDD;
        case protocol::REPORT_VSWING_MID:
            return vertical_swing_options::MID;
        case protocol::REPORT_VSWING_MIDU:
            return vertical_swing_options::MIDU;
        case protocol::REPORT_VSWING_UP:
            return vertical_swing_options::UP;
        case protocol::REPORT_VSWING_CDOWN:
            return vertical_swing_options::CDOWN;
        case protocol::REPORT_VSWING_CMIDD:
            return vertical_swing_options::CMIDD;
        case protocol::REPORT_VSWING_CMID:
            return vertical_swing_options::CMID;
        case protocol::REPORT_VSWING_CMIDU:
            return vertical_swing_options::CMIDU;
        case protocol::REPORT_VSWING_CUP:
            return vertical_swing_options::CUP;
        default:
            ESP_LOGW(TAG, "Received unknown vertical swing mode");
            return vertical_swing_options::OFF;;
    }
}

std::string SinclairACCNT::determine_horizontal_swing()
{
    uint8_t mode = (this->serialProcess_.data[protocol::REPORT_HSWING_BYTE]  & protocol::REPORT_HSWING_MASK) >> protocol::REPORT_HSWING_POS;

    switch (mode) {
        case protocol::REPORT_HSWING_OFF:
            return horizontal_swing_options::OFF;
        case protocol::REPORT_HSWING_FULL:
            return horizontal_swing_options::FULL;
        case protocol::REPORT_HSWING_CLEFT:
            return horizontal_swing_options::CLEFT;
        case protocol::REPORT_HSWING_CMIDL:
            return horizontal_swing_options::CMIDL;
        case protocol::REPORT_HSWING_CMID:
            return horizontal_swing_options::CMID;
        case protocol::REPORT_HSWING_CMIDR:
            return horizontal_swing_options::CMIDR;
        case protocol::REPORT_HSWING_CRIGHT:
            return horizontal_swing_options::CRIGHT;
        default:
            ESP_LOGW(TAG, "Received unknown horizontal swing mode");
            return horizontal_swing_options::OFF;
    }
}

std::string SinclairACCNT::determine_display()
{
    uint8_t mode = (this->serialProcess_.data[protocol::REPORT_DISP_MODE_BYTE] & protocol::REPORT_DISP_MODE_MASK) >> protocol::REPORT_DISP_MODE_POS;

    this->display_power_internal_ = (this->serialProcess_.data[protocol::REPORT_DISP_ON_BYTE] & protocol::REPORT_DISP_ON_MASK);

    switch (mode) {
        case protocol::REPORT_DISP_MODE_AUTO:
            this->display_mode_internal_ = display_options::AUTO;
            break;
        case protocol::REPORT_DISP_MODE_SET:
            this->display_mode_internal_ = display_options::SET;
            break;
        case protocol::REPORT_DISP_MODE_ACT:
            this->display_mode_internal_ = display_options::ACT;
            break;
        case protocol::REPORT_DISP_MODE_OUT:
            this->display_mode_internal_ = display_options::OUT;
            break;
        default:
            ESP_LOGW(TAG, "Received unknown display mode");
            this->display_mode_internal_ = display_options::AUTO;
            break;
    }

    if (this->display_power_internal_)
    {
        return this->display_mode_internal_;
    }
    else
    {
        return display_options::OFF;
    }
}

std::string SinclairACCNT::determine_display_unit()
{
    if (this->serialProcess_.data[protocol::REPORT_DISP_F_BYTE] & protocol::REPORT_DISP_F_MASK)
    {
        return display_unit_options::DEGF;
    }
    else
    {
        return display_unit_options::DEGC;
    }
}

bool SinclairACCNT::determine_plasma(){
    bool plasma1 = (this->serialProcess_.data[protocol::REPORT_PLASMA1_BYTE] & protocol::REPORT_PLASMA1_MASK) != 0;
    bool plasma2 = (this->serialProcess_.data[protocol::REPORT_PLASMA2_BYTE] & protocol::REPORT_PLASMA2_MASK) != 0;
    return plasma1 || plasma2;
}

bool SinclairACCNT::determine_sleep(){
    return (this->serialProcess_.data[protocol::REPORT_SLEEP_BYTE] & protocol::REPORT_SLEEP_MASK) != 0;
}

bool SinclairACCNT::determine_xfan(){
    return (this->serialProcess_.data[protocol::REPORT_XFAN_BYTE] & protocol::REPORT_XFAN_MASK) != 0;
}

bool SinclairACCNT::determine_save(){
    return (this->serialProcess_.data[protocol::REPORT_SAVE_BYTE] & protocol::REPORT_SAVE_MASK) != 0;
}


/*
 * Sensor handling
 */

void SinclairACCNT::on_vertical_swing_change(const std::string &swing)
{
    if (this->state_ != ACState::Ready)
        return;

    ESP_LOGD(TAG, "Setting vertical swing position");

    this->update_ = ACUpdate::UpdateStart;
    this->vertical_swing_state_ = swing;
}

void SinclairACCNT::on_horizontal_swing_change(const std::string &swing)
{
    if (this->state_ != ACState::Ready)
        return;

    ESP_LOGD(TAG, "Setting horizontal swing position");

    this->update_ = ACUpdate::UpdateStart;
    this->horizontal_swing_state_ = swing;
}

void SinclairACCNT::on_display_change(const std::string &display)
{
    if (this->state_ != ACState::Ready)
        return;

    ESP_LOGD(TAG, "Setting display mode");

    this->update_ = ACUpdate::UpdateStart;
    this->display_state_ = display;
}

void SinclairACCNT::on_display_unit_change(const std::string &display_unit)
{
    if (this->state_ != ACState::Ready)
        return;

    ESP_LOGD(TAG, "Setting display unit");

    this->update_ = ACUpdate::UpdateStart;
    this->display_unit_state_ = display_unit;
}

void SinclairACCNT::on_plasma_change(bool plasma)
{
    if (this->state_ != ACState::Ready)
        return;

    ESP_LOGD(TAG, "Setting plasma");

    this->update_ = ACUpdate::UpdateStart;
    this->plasma_state_ = plasma;
}

void SinclairACCNT::on_beeper_change(bool beeper)
{
    /* Write-only flag carried in every SET packet; no change packet needed and
       no need to wait for the unit to be ready. */
    ESP_LOGD(TAG, "Setting beeper %s", beeper ? "on" : "off");

    this->beeper_state_ = beeper;
}

void SinclairACCNT::on_sleep_change(bool sleep)
{
    if (this->state_ != ACState::Ready)
        return;

    ESP_LOGD(TAG, "Setting sleep");

    this->update_ = ACUpdate::UpdateStart;
    this->sleep_state_ = sleep;
}

void SinclairACCNT::on_xfan_change(bool xfan)
{
    if (this->state_ != ACState::Ready)
        return;

    ESP_LOGD(TAG, "Setting xfan");

    this->update_ = ACUpdate::UpdateStart;
    this->xfan_state_ = xfan;
}



void SinclairACCNT::on_save_change(bool save)
{
    if (this->state_ != ACState::Ready)
        return;

    ESP_LOGD(TAG, "Setting save");

    this->update_ = ACUpdate::UpdateStart;
    this->save_state_ = save;
}

}  // namespace CNT
}  // namespace sinclair_ac
}  // namespace esphome
