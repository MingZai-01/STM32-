#include "stm32f10x.h"                  // Device header
#include "Delay.h"
#include "OLED.h"
#include "Button.h"

uint16_t Count;

//主函数实现了单击按键Count+1，长按按键Count连续+1（间隔100ms），双击按键Count+100
int main(void)
{
	OLED_Init();
	Button_Init();
	OLED_ShowString(1, 1, "CNT:");						//Count值
	OLED_ShowString(2, 1, "ContiPrsTim:");				//长按时间
	OLED_ShowString(3, 1, "ContiPrsFlag:");				//长按标志位
	OLED_ShowString(4, 1, "DoubPrsTim:");				//离上一次按键的时间
	while(1)
	{
		if(Button_SingPrsFlag || Button_contiPrsFlag)	//如果单击或长按
		{
			Count++;
			Button_SingPrsFlag = 0;						//手动清零单击标志位
			if(Button_contiPrsFlag)						//如果是长按还要延时100ms
				Delay_ms(100);
		}
		if(Button_DoubPrsFlag)							//如果是双击
		{
			Count += 100;
			Button_DoubPrsFlag = 0;						//手动清零双击标志位
		}
		OLED_ShowNum(1, 5, Count, 5);
		OLED_ShowNum(2, 13, Button_ContiPrsCount, 4);
		OLED_ShowNum(3, 14, Button_contiPrsFlag, 2);
		OLED_ShowNum(4, 12, Button_DoubPrsCount, 4);
	}
}
