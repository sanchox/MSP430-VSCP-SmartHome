// Skeleton form:
// http://www.vscp.org/wiki/doku.php/howto/how_to_port_vscp_to_new_firmware

#include "..\..\VSCP\firmware\common\vscp_firmware.h"
#include "NAPSocket\include\NAPSocket_library.h"
#include "flash_operation.h"
#include "io430.h"
#include <stdio.h>

volatile unsigned int measurement_clock = 0;

void led_init() { P4DIR |= 0x01 | 0x02; }

void led1_on() { P4OUT |= 0x01; }
void led1_off() { P4OUT &= ~0x01; }
void led1_toggle() { P4OUT ^= 0x01; }

void button_init() {
  P2DIR &= ~(0x02 | 0x04);
  P2REN |= 0x02 | 0x04;
  P2OUT |= 0x02 | 0x04;
}

unsigned char button1_state() { return ((P2IN & 0x02) >> 1); }

int main(void) {
  // Stop watchdog timer to prevent time out reset
  WDTCTL = WDTPW + WDTHOLD;

  button_init();
  led_init();

  // Check VSCP persistent storage and
  // restore if needed
  if (!vscp_check_pstorage()) {

    flash_write_byte(&INFO_SEGMENT[VSCP_REG_ALARMSTATUS], 0x00);

    flash_write_byte(&INFO_SEGMENT[VSCP_REG_VSCP_MAJOR_VERSION], 0x01);
    flash_write_byte(&INFO_SEGMENT[VSCP_REG_VSCP_MINOR_VERSION], 0x00);

    flash_write_byte(&INFO_SEGMENT[VSCP_REG_MANUFACTUR_ID0], 0x00);
    flash_write_byte(&INFO_SEGMENT[VSCP_REG_MANUFACTUR_ID1], 0x00);
    flash_write_byte(&INFO_SEGMENT[VSCP_REG_MANUFACTUR_ID2], 0x00);
    flash_write_byte(&INFO_SEGMENT[VSCP_REG_MANUFACTUR_ID3], 0x00);
    flash_write_byte(&INFO_SEGMENT[VSCP_REG_MANUFACTUR_SUBID0], 0x00);
    flash_write_byte(&INFO_SEGMENT[VSCP_REG_MANUFACTUR_SUBID1], 0x00);
    flash_write_byte(&INFO_SEGMENT[VSCP_REG_MANUFACTUR_SUBID2], 0x00);
    flash_write_byte(&INFO_SEGMENT[VSCP_REG_MANUFACTUR_SUBID3], 0x00);

    flash_write_byte(&INFO_SEGMENT[VSCP_REG_PAGE_SELECT_MSB], 0x00);
    flash_write_byte(&INFO_SEGMENT[VSCP_REG_PAGE_SELECT_LSB], 0x00);

    flash_write_byte(&INFO_SEGMENT[VSCP_REG_FIRMWARE_MAJOR_VERSION], 0x01);
    flash_write_byte(&INFO_SEGMENT[VSCP_REG_FIRMWARE_MINOR_VERSION], 0x00);
    flash_write_byte(&INFO_SEGMENT[VSCP_REG_FIRMWARE_SUB_MINOR_VERSION], 0x00);

    flash_write_byte(&INFO_SEGMENT[VSCP_REG_BOOT_LOADER_ALGORITHM],
                     VSCP_BOOTLOADER_NONE);

    flash_write_byte(&INFO_SEGMENT[VSCP_REG_BUFFER_SIZE], 0x00);

    flash_write_byte(&INFO_SEGMENT[VSCP_REG_PAGES_USED], 0x00);

    for (int i = 0; i < 7; ++i)
      flash_write_byte(&INFO_SEGMENT[VSCP_REG_GUID + i], 0xFF);
    flash_write_byte(&INFO_SEGMENT[VSCP_REG_GUID + 7], 0xFC);
    for (int i = 8; i < 15; ++i)
      flash_write_byte(&INFO_SEGMENT[VSCP_REG_GUID + i], 0x00);
    flash_write_byte(&INFO_SEGMENT[VSCP_REG_GUID + 15], 0x01);

    flash_write_byte(&INFO_SEGMENT[VSCP_REG_DEVICE_URL], 0x00);
  }

  vscp_init(); // Initialize the VSCP functionality

  TA1CCTL0 = CCIE; // CCR0 interrupt enabled
  TA1CCR0 = 32;
  TA1CTL = TASSEL_1 + MC_1 + TACLR; // ACLK, upmode, clear TAR

  __enable_interrupt();

  while (1) {
    //  printf("Hello world\r\n");
    if ((vscp_initbtncnt > 500) && (vscp_node_state != VSCP_STATE_INIT)) {
      // Init button pressed
      vscp_nickname = VSCP_ADDRESS_FREE;
      vscp_writeNicknamePermanent(VSCP_ADDRESS_FREE);
      vscp_init();
    }
    // Check for a valid event
    vscp_imsg.flags = 0;
    vscp_getEvent();
    // do a meaurement if needed
    if (measurement_clock > 1000) {
      measurement_clock = 0;
      // Do VSCP one second jobs
      vscp_doOneSecondWork();
      switch (vscp_node_state) {
      case VSCP_STATE_STARTUP: // Cold/warm reset
        // Get nickname from EEPROM
        if (VSCP_ADDRESS_FREE == vscp_nickname) {
          // new on segment need a nickname
          vscp_node_state = VSCP_STATE_INIT;
        } else {
          // been here before - go on
          vscp_node_state = VSCP_STATE_ACTIVE;
          vscp_goActiveState();
        }
        break;
      case VSCP_STATE_INIT: // Assigning nickname
        vscp_handleProbeState();
        break;
      case VSCP_STATE_PREACTIVE: // Waiting for host initialisation
        vscp_goActiveState();
        break;
      case VSCP_STATE_ACTIVE:                   // The normal state
        if (vscp_imsg.flags & VSCP_VALID_MSG) { // incoming message?
          vscp_handleProtocolEvent();
        }
        break;
      case VSCP_STATE_ERROR: // Everything is *very* *very* bad.
        vscp_error();
        break;
      default: // Should not be here...
        vscp_node_state = VSCP_STATE_STARTUP;
        break;
      }
      //   doWork(); // Do general application work
    }
  }
  // return 0;
}

// Timer A0 interrupt service routine
#pragma vector = TIMER1_A0_VECTOR
__interrupt void TIMER1_A0_ISR(void) {
  ++vscp_timer;
  ++measurement_clock;
  ++vscp_statuscnt;

  if (button1_state()) {
    vscp_initbtncnt = 0;
  } else {
    ++vscp_initbtncnt;
  }

  switch (vscp_initledfunc) {
  case VSCP_LED_BLINK1:
    if (vscp_statuscnt > 100) {
      led1_toggle();
      vscp_statuscnt = 0;
    }
    break;
  case VSCP_LED_ON:
    led1_on();
    break;
  case VSCP_LED_OFF:
    led1_off();
    break;
  }
}
