/*
本模块实现了单个按键的单击、双击、长按检测，并且保证了每次只会有单击、双击、长按这三种中的一种被判定
和双击冷却机制，即快速连续按按键不会被判定为连续的双击。（也不会被判定为连续的单击。。。）
*/
#include "stm32f10x.h"                  // Device header

#define DOUBLE_PRS_TIME			4		//双击检测的标准间隔时间，单位：*20ms

//以下全部为外部全局变量
uint8_t Button_SingPrsFlag = 0;			//单击标志位，单击时置为1，不会自动清零，需要手动清零
uint8_t Button_DoubPrsFlag = 0;			//双击标志位，双击时置为1，不会自动清零，需要手动清零
uint8_t Button_contiPrsFlag = 0;		//长按标志位，长按时为1，抬起时自动置0
uint16_t Button_DoubPrsCount = 0;		//按键间隔计时（服务于双击判定），实际运用此模块时可将此变量设置为内部静态变量，这个程序中设为外部变量是方便显示在OLED上调试
uint16_t Button_ContiPrsCount = 0;		//长按计时，实际运用此模块时可将此变量设置为内部静态变量，这个程序中设为外部变量是方便显示在OLED上调试

/**
  * @brief  按键初始化，包括初始化按键PA0和TIM2初始化、TIM2更新中断初始化，即每20ms进入一次中断
  * @param  无
  * @retval 无
  */
void Button_Init(void)
{
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
	
	GPIO_InitTypeDef GPIO_InitStruct;
	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_InitStruct.GPIO_Pin = GPIO_Pin_0;
	GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStruct);
	
	TIM_InternalClockConfig(TIM2);
	
	TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStrcut;
	TIM_TimeBaseInitStrcut.TIM_ClockDivision = TIM_CKD_DIV1;
	TIM_TimeBaseInitStrcut.TIM_CounterMode = TIM_CounterMode_Up;
	TIM_TimeBaseInitStrcut.TIM_Period = 200 - 1;					//20ms更新中断一次
	TIM_TimeBaseInitStrcut.TIM_Prescaler = 7200 - 1;
	TIM_TimeBaseInitStrcut.TIM_RepetitionCounter = 0x00;
	TIM_TimeBaseInit(TIM2, &TIM_TimeBaseInitStrcut);
	
	TIM_ClearFlag(TIM2, TIM_FLAG_Update);
	
	TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);
	
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
	NVIC_InitTypeDef NVIC_InitStruct;
	NVIC_InitStruct.NVIC_IRQChannel = TIM2_IRQn;
	NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;
	NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = 0;
	NVIC_InitStruct.NVIC_IRQChannelSubPriority = 0;
	NVIC_Init(&NVIC_InitStruct);
	
	TIM_Cmd(TIM2, ENABLE);
}

//该模块保证了每次只会有单击、双击、长按这三种中的一种被判定，这些实现了防冲突或者说互斥作用的设计
//用含有“#”的注释标注出来了。
//这些代码保证了：双击不会触发单击的操作；长按的最后不会触发单击操作；长按结束后立即快速单击不会被
//判定为双击。这也就是说，如果没有这些设计，那么以主函数的累加程序举例，每次双击不仅执行双击操作还
//执行2次单击操作，也就是Count不仅+100而且还要+1+1；长按的最后还会触发一次单击操作，也就是长按连续
//累加最后抬起按键会比预想的多加1（虽然这个问题不是很明显，但是如果对比一下就知道其实还是挺明显
//的）；长按结束后立即快速单击还会判定一次双击，也就是在长按连续累加的末尾快速按一下按键又会直接加
//100。
//其中单击与双击的互斥采用的方法是延迟单击判定和设置历史标志位，延迟判定就是单击之后会延迟一个双击
//判定的时间来看看是否构成双击，若不构成双击再判定为单击。（延迟判定法带来的不便就是如果以小于延迟
//时间的速度快速单击按键则不会被判定为连续的单击）延迟判定法只能确保双击的第一次不被判为单击，所以
//第二次就用设置历史标志位来处理互斥。长按与单击、长按与双击的互斥都是凭借设置历史标志位来实现的。
//这个其实比单击双击互斥简单多了。
//通过这些设计，实现了以下优先级：长按优先于双击优先于单击。
void TIM2_IRQHandler(void)
{
	if(TIM_GetITStatus(TIM2, TIM_IT_Update) == SET)
	{
		static uint8_t Button_State = 0;						//0表示空闲状态
		static uint8_t Button_DoubCD = 0;						//双击的冷却时间标志位（为了避免快速连续按按键导致连续判定为双击）
		static uint8_t Button_SingPrsPreFlag = 0;				//#单击预标志位。按下、抬起时，代替原来的单击标志位变成1，在下一个空闲计时后，确保不会发生双击才会置真正的单击标志位
		static uint8_t Button_DoubMemory = 0;					//#双击历史标志位，此标志位为1则说明上一个按键的动作是双击
		static uint8_t Button_ContiMemory = 0;					//#长按历史标志位，此标志位为1则说明上一个按键的动作为长按
		if(Button_State == 0)									//空闲状态
		{
			if(GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_0) == 1)	//在空闲状态，每一次中断都会检测是否按下，当未按下时，每一次中断，双击检测计时加一，按下后这个计时清零，这样就可以计时与上一次按键按下的时间间隔了
			{
				if(Button_DoubPrsCount <= DOUBLE_PRS_TIME + 5)				//只有在计时小于（DOUBLE_PRS_TIME + 5）*20ms时才会计时，大于的话不记时，因为时间已经太长无意义了			
					Button_DoubPrsCount++;				
				if(Button_DoubPrsCount > DOUBLE_PRS_TIME + 5)					//当计数时长大于（DOUBLE_PRS_TIME + 5）*20ms时，即两次按键的时间间隔大于（DOUBLE_PRS_TIME + 5）*20ms时cd清零，双击才会有效
					Button_DoubCD = 0;
				if(Button_DoubPrsCount > DOUBLE_PRS_TIME && Button_SingPrsPreFlag == 1)		//#这里单击标志位才会真正的赋值
				{
					Button_SingPrsFlag = 1;
					Button_SingPrsPreFlag = 0;												//#赋值后将单机预标志位清零
				}
					
			}
			if(GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_0) == 0)	//当空闲状态检测到低电平时转换为按下状态（注意与抬起区分）
			{
				if(Button_DoubPrsCount <= DOUBLE_PRS_TIME)
				{
					if(Button_DoubCD == 0 && Button_ContiMemory != 1)	//#只有CD为0且两次按键间隔小于DOUBLE_PRS_TIME*20ms且上一次动作不是长按时才能置双击标志位
					{
						Button_DoubPrsFlag = 1;					//置双击标志位	
						Button_DoubCD = 1;						//置1次双击标志位之后进入CD
						Button_DoubMemory = 1;					//#置双击历史标志位为1
						Button_SingPrsPreFlag = 0;				//#清零单击预标志位
					}
				}
				Button_ContiMemory = 0;							//#这里再清零长按历史标志位
				Button_State = 1;								//转换状态
			}
		}
		else if(Button_State == 1)								//1表示按下状态
		{
			if(GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_0) == 0)	//在按下状态，每一次中断都检测是否抬起，若未抬起则每次中断（20ms）长按计数加一，并且判断长按
			{
				Button_ContiPrsCount++;
				if(Button_ContiPrsCount >= 25)					//按下时长大于25个中断也就是500ms则被识别为长按
				{
					Button_contiPrsFlag = 1;
					Button_ContiMemory = 1;						//#置长按历史标志位为1
				}	
			}
			if(GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_0) == 1)	//检测到抬起
			{
				if(Button_DoubMemory != 1 && Button_ContiMemory != 1)	//#只有当上一个动作既不是双击又不是长按才会判定为单击
					Button_SingPrsPreFlag = 1;
				else
					Button_DoubMemory = 0;								//#这里只清零双击历史标志位，先不清零长按历史标志位，因为要留着给双击判断
				Button_contiPrsFlag = 0;								//清零长按标志位（因为抬起了，长按结束了）		
				Button_ContiPrsCount = 0;								//清零长按计时
				Button_State = 0;										//转换状态
				Button_DoubPrsCount = 0;								//清零双击计时
			}
		}
		
		TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
	}
}
