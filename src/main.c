#include "main.h"
#include "uart.h"

#define DATA P2
#define INTn P3_6
#define A0 P3_7
#define CSn P4_1
#define RDn P4_2
#define WRn P4_4
#define LEDn P3_3

uint8_t __code DeviceDescriptor[18] = {
    18,         // bLength
    0x01,       // bDescriptorType
    0x00, 0x02, // bcdUSB
    0x00,       // bDeviceClass
    0x00,       // bDeviceSubClass
    0x00,       // bDeviceProtocol
    8,          // bMaxPacketSize0
    0x34, 0x12, // idVentor
    0x78, 0x56, // idProduct
    0x01, 0x00, // bcdDevice
    0,          // iManufacturer
    0,          // iProduct
    0,          // iSerialNumber
    1,          // bNumConfiguration
};

#define wVal(x) ((x)&0xFF), (((x) >> 8) & 0xFF)
#define CFGDES_SIZE (9 + 9 + 7 + 7)

uint8_t __code ConfigurationDescriptor[CFGDES_SIZE] = {
    9,                 // bLength
    0x02,              // bDescriptorType
    wVal(CFGDES_SIZE), // wTotalLength
    1,                 // bNumInterfaces
    1,                 // bConfigurationValue
    0,                 // iConfiguration
    0x80,              // bmAttributes
    250,               // bMaxPower
    // interface
    9,    // bLength
    0x04, // bDescriptorType
    0,    // bInterfaceNumber
    0,    // bAlternateSetting
    2,    // bNumEndpoints
    0xFF, // bInterfaceClass
    0xFF, // bInterfaceSubClass
    0xFF, // bInterfaceProtocol
    0,    // iInterface
    // ep1 in
    7,       // bLength
    0x05,    // bDescriptorType
    0x81,    // bEndpointAddress(IN1)
    0x03,    // bmAttributes(INTERRUPT)
    wVal(1), // wMaxPacketSize
    0x06,    // bInterval (2^(n-1))=(2^(6-1))=32ms
    // ep1 out
    7,       // bLength
    0x05,    // bDescriptorType
    0x01,    // bEndpointAddress(OUT1)
    0x03,    // bmAttributes(INTERRUPT)
    wVal(1), // wMaxPacketSize
    0x00,    // bInterval
};

uint8_t CurrentSetupRequest = 0;
const uint8_t *CurrentDescriptor;
uint8_t CurrentDescriptor_Sent = 0;
uint8_t CurrentDescriptor_Size = 0;
uint8_t DeviceAddress = 0;

__bit configured = 0;

void Delay30ms() //@22.1184MHz
{
    unsigned char i, j, k;

    _nop_();
    _nop_();
    i = 3;
    j = 134;
    k = 115;
    do
    {
        do
        {
            while (--k)
                ;
        } while (--j);
    } while (--i);
}

void Delay1us() //@22.1184MHz
{
    unsigned char i;

    i = 3;
    while (--i)
        ;
}

void wr_cmd(uint8_t cmd)
{
    P2M0 = 0xFF;
    A0 = 1;
    DATA = cmd;
    Delay1us();
    WRn = 0;
    Delay1us();
    WRn = 1;
    Delay1us();
    A0 = 0;
    DATA = 0xFF;
    Delay1us();
}

void wr_data(uint8_t data)
{
    P2M0 = 0xFF;
    A0 = 0;
    DATA = data;
    Delay1us();
    WRn = 0;
    Delay1us();
    WRn = 1;
    Delay1us();
    A0 = 0;
    DATA = 0xFF;
    Delay1us();
}

void rd_data(uint8_t *data)
{
    P2M0 = 0x00;
    DATA = 0xFF;
    A0 = 0;
    Delay1us();
    RDn = 0;
    Delay1us();
    *data = DATA;
    RDn = 1;
    Delay1us();
    A0 = 0;
    Delay1us();
}

void sysinit()
{
    P2M1 = 0b00000000;
    P2M0 = 0b00000000;
    P3M1 = 0b00000000;
    P3M0 = 0b10000000;
    P4M1 = 0b00000000;
    P4M0 = 0b00010110;

    EA = 1;
    UartInit();
    //32毫秒@22.1184MHz
    AUXR &= 0x7F; //定时器时钟12T模式
    TMOD &= 0xF0; //设置定时器模式
    TL0 = 0x9A;   //设置定时初值
    TH0 = 0x19;   //设置定时初值
    TF0 = 0;      //清除TF0标志
    TR0 = 1; //定时器0开始计时
    ET0 = 1;

    CSn = 1;
    DATA = 0xFF;
    INTn = 1;
    RDn = 1;
    WRn = 1;
    A0 = 0;

    print("start...\r\n");
    Delay30ms();
    Delay30ms();
    Delay30ms();
    Delay30ms();
}

uint8_t poll_interrupt()
{
    uint8_t i;
    while (INTn)
    {
    }
    wr_cmd(0x22); // get status
    rd_data(&i);
    return i;
}

void main()
{
    uint8_t i, len;
    uint8_t buf[8];
    __bit setup_error;

    sysinit();

    CSn = 0;
    wr_cmd(0x05);
    CSn = 1;
    Delay30ms();
    Delay30ms();

    CSn = 0;

    wr_cmd(0x06);
    wr_data(0x57);
    rd_data(&i);
    haltif(i != 0xA8, "check exist error");
    print("check ok\r\n");

    wr_cmd(0x15);
    wr_data(0x01);
    for (;;)
    {
        rd_data(&i);
        if (i == 0x51)
        {
            break;
        }
    }

    while (1)
    {
        i = poll_interrupt();
        switch (i)
        {
        case 0x0C:        // ep0 setup
            wr_cmd(0x28); // rd usb data
            rd_data(&i);  // read length
            setup_error = 1;
            if (i == 8) // 数据长度一定是8
            {
                // 读8字节的数据
                for (i = 0; i < 8; i++)
                {
                    rd_data(buf + i);
                }
                CurrentSetupRequest = buf[1]; // bRequest
                print("SETUP ");
                print_8x(CurrentSetupRequest);
                print("\r\n");
                switch (CurrentSetupRequest)
                {
                case 0x06:      // get descriptor
                    i = buf[3]; // descriptor type
                    switch (i)
                    {
                    case 0x01: // device descriptor
                        CurrentDescriptor = DeviceDescriptor;
                        CurrentDescriptor_Size = 18;
                        break;
                    case 0x02: // configuration descriptor
                        CurrentDescriptor = ConfigurationDescriptor;
                        CurrentDescriptor_Size = (buf[6] < CFGDES_SIZE) ? buf[6] : CFGDES_SIZE;
                        break;
                    default:
                        CurrentDescriptor = 0;
                        break;
                    }
                    if (CurrentDescriptor)
                    {
                        wr_cmd(0x29); // wr usb data3
                        wr_data(8);
                        for (i = 0; i < 8; i++)
                        {
                            wr_data(CurrentDescriptor[i]);
                        }
                        CurrentDescriptor_Sent = 8;
                        setup_error = 0;
                    }
                    wr_cmd(0x23); // unlock usb
                    break;
                case 0x05: // set address
                    DeviceAddress = buf[2];
                    setup_error = 0;
                    wr_cmd(0x29); // wr usb data3
                    wr_data(0);
                    wr_cmd(0x23); // unlock usb
                    break;
                case 0x09: // set configuration
                    setup_error = 0;
                    wr_cmd(0x29); // wr usb data3
                    wr_data(0);
                    wr_cmd(0x23); // unlock usb
                    configured = 1;
                    break;
                }
            }
            if (setup_error)
            {
                wr_cmd(0x19);  // set endp 3
                wr_data(0x0F); // stall
                wr_cmd(0x23);  // unlock usb
            }
            break;
        case 0x08:        // ep0 in
            wr_cmd(0x23); // unlock usb
            if ((CurrentSetupRequest == 0x06) && (CurrentDescriptor))
            {
                len = CurrentDescriptor_Size - CurrentDescriptor_Sent;
                len = (len > 8) ? 8 : len;
                wr_cmd(0x29); // wr usb data3
                wr_data(len);
                for (i = 0; i < len; i++)
                {
                    wr_data(CurrentDescriptor[CurrentDescriptor_Sent]);
                    CurrentDescriptor_Sent++;
                }
            }
            else if (CurrentSetupRequest == 0x05)
            {
                wr_cmd(0x13);
                wr_data(DeviceAddress);
            }
            break;
        case 0x00:        // ep0 out
            wr_cmd(0x23); // unlock usb
            break;
        case 0x01: // ep1 out
            wr_cmd(0x28); // rd usb data
            rd_data(&len);  // read length
            for (i = 0; i < len; i++)
            {
                rd_data(buf + i);
            }
            if (len == 1)
            {
                LEDn = !(buf[0] & 0x01); // 最低位1亮
            }
            break;
        default:
            // if ((i & 0x03) == 0x03)
            // {
            //     print("bus reset\r\n");
            // }
            wr_cmd(0x23); // unlock usb
            rd_data(&i);  // dummy
            break;
        }
    }
}

uint8_t senddata = 0;
void T0_Isr() __interrupt(1)
{
    if (configured)
    {
        wr_cmd(0x2A);
        wr_data(1);
        wr_data(senddata++);
    }
}