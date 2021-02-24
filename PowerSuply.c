/*
 * PowerSuply.c
 * Created: 14.10.2019 22:16:39
 * Author: Касиян Алексей
 * CPU = ATMega8A
 * intern 8MGz
 * FUSE H: D9
 * FUSE L: 64
 */ 

#define F_CPU 8000000L
#include <avr/io.h>
#include <util/delay.h>
#include <stdbool.h>
#include <avr/interrupt.h>

// кнопка питания с подсветкой
#define LED_PORT		PORTB
#define MASK_LED		1							// подсветка кнопки
#define led_off()		LED_PORT &=~ (1<<MASK_LED)
#define led_on()		LED_PORT |= (1<<MASK_LED)
//#define led_switch()	LED_PORT ^= MASK_LED
#define button_power	(PIND & 0x80)				// кнопка питания

// динамическая индикация
#define digit_PORT PORTB
#define digit1 0b11011111		//5
#define digit2 0b11101111		//4
#define digit3 0b10111111		//6
#define digit4 0b01111111		//7

// HC595
#define HC595_DATA_PORT PORTC
#define HC595_DATA_PIN  2
#define HC595_CLK_PORT  PORTC
#define HC595_CLK_PIN   4
#define HC595_LOAD_PORT PORTC
#define HC595_LOAD_PIN  3

#define HC595_DATA_low()  HC595_DATA_PORT &=~ (1<<HC595_DATA_PIN);
#define HC595_DATA_high() HC595_DATA_PORT |=  (1<<HC595_DATA_PIN);

#define HC595_CLK_low()   HC595_CLK_PORT &=~ (1<<HC595_CLK_PIN);
#define HC595_CLK_high()  HC595_CLK_PORT |=  (1<<HC595_CLK_PIN);

#define HC595_LOAD_low()  HC595_LOAD_PORT &=~ (1<<HC595_LOAD_PIN);
#define HC595_LOAD_high() HC595_LOAD_PORT |=  (1<<HC595_LOAD_PIN);

// encoder
#define ENCODER_PIN			PIND
#define ENCODER_BUTTON		0x10	// вход к которому подключена кнопка енкодера
#define ENCODER_LEFT		0x04	// вход к которому подключен один выход енкодера
#define ENCODER_RIGHT		0x08	// вход к которому подключен второй выход енкодера
// флаги состояния енкодера
#define ENCODER_MEASURE		0x01	// разрешение на измерения состояния енкодера
#define ENCODER_LAST_STATE	0x02	// последнее стабильное состояние енкодера
int encoder_state = 0;				// переменная, в котой хранится состояние енкодера

// реле питания блока питания
#define RELE_POWER_PORT		PORTD
#define RELE_POWER_PIN		6
#define RELE_POWER_ON()		RELE_POWER_PORT |=  (1<<RELE_POWER_PIN)		// включить реле
#define RELE_POWER_OFF()	RELE_POWER_PORT &=~ (1<<RELE_POWER_PIN)		// отключить реле

// кнопки управления блоком питания DP3005
#define BUTTON_INC_PORT		PORTC
#define BUTTON_INC_PIN		0
#define BUTTON_DEC_PORT		PORTC
#define BUTTON_DEC_PIN		1
#define BUTTON_SET_PORT		PORTB
#define BUTTON_SET_PIN		2
#define BUTTON_ON_OFF_PORT	PORTB
#define BUTTON_ON_OFF_PIN	0



//-------------------------------------------------------------------------------------------------
// глобальные переменные

unsigned int mili_seconds = 0;
uint8_t seconds = 0;
uint8_t minutes = 0;
uint8_t hours = 0;
uint8_t count_seconds = 0;
unsigned int count = 0;

// Этот массив содержит сегменты, которые необходимо зажечь для отображения на индикаторе цифр 0-9 (0-горит символ, 1-погашен символ)
// 0 1 2 3 4 5 6 7 8 9
// L E P -
uint8_t digits[16] = {
	0b00101000, 0b11101011, 0b00110010, 0b10100010, 0b11100001, 0b10100100, 0b00100100, 0b11101010, 0b00100000, 0b10100000, 0b11011111,
	0b11101011, 0b00110100, 0b01110000, 0b11110111};	// edhcgbfa

// динамическая индикация
int time = 0;												// число, которое выводится на индикатор
int digit_place = 0;										// место цифры, которая выводится в данный момент
unsigned int razr1 = 0, razr2 = 0, razr3 = 0, razr4 = 0;	// разбивка числа на цифры по весовым категориям

uint8_t encoder_counter = 0;
//-------------------------------------------------------------------------------------------------
// процедуры для работы с переферией 

//-----------------------------------------------
void push_inc (void)
{
	BUTTON_INC_PORT |= (1<<BUTTON_INC_PIN);
	_delay_ms(100);
	BUTTON_INC_PORT &=~ (1<<BUTTON_INC_PIN);
}

void push_dec (void)
{
	BUTTON_DEC_PORT |= (1<<BUTTON_DEC_PIN);
	_delay_ms(100);
	BUTTON_DEC_PORT &=~ (1<<BUTTON_DEC_PIN);
}

int encoder (void)
{			
// переходное состояние енкодера
	if( !(ENCODER_PIN & ENCODER_RIGHT) && (ENCODER_PIN & ENCODER_LEFT) && (encoder_state & ENCODER_MEASURE) )	// R=0, count = 2000
	{
		_delay_ms(1);
		if(!(ENCODER_PIN & ENCODER_RIGHT) && (ENCODER_PIN & ENCODER_LEFT))
		{
			if(encoder_state & ENCODER_LAST_STATE)
			{
				encoder_counter--;
				push_dec();														// нажимаем кнопку "DEC"
			}
			else
			{
				encoder_counter++;
				push_inc();														// нажимаем кнопку "INC"
			}
					
			encoder_state &=~ ENCODER_MEASURE;									// запрещаем обработку вращения енкодера до следующего устойчивого состояния
			_delay_ms(1);
		}
	}
	if( !(ENCODER_PIN & ENCODER_LEFT) && (ENCODER_PIN & ENCODER_RIGHT) && (encoder_state & ENCODER_MEASURE) )		// L=0, count = 1000
	{
		_delay_ms(1);
		if(!(ENCODER_PIN & ENCODER_LEFT) && (ENCODER_PIN & ENCODER_RIGHT))
		{
			if(encoder_state & ENCODER_LAST_STATE)
			{
				encoder_counter++;
				push_inc();														// нажимаем кнопку "INC"
			}
			else
			{
				encoder_counter--;
				push_dec();														// нажимаем кнопку "DEC"
			}
					
			encoder_state &=~ ENCODER_MEASURE;									// запрещаем обработку вращения енкодера до следующего устойчивого состояния
			_delay_ms(1);
		}
	}

// устойчивое состояние енкодера
	if( (!(ENCODER_PIN & ENCODER_LEFT)) && (!(ENCODER_PIN & ENCODER_RIGHT)) )	// L=0, R=0
	{
		_delay_ms(1);
		if( (!(ENCODER_PIN & ENCODER_LEFT)) && (!(ENCODER_PIN & ENCODER_RIGHT)) )
		{
			encoder_state &=~ ENCODER_LAST_STATE;							// текущее устойчивое состояние енкодера 0
			encoder_state |= ENCODER_MEASURE;								// разрешаем определения направления вращения енкодера
		}
	}
	if((ENCODER_PIN & ENCODER_LEFT) && (ENCODER_PIN & ENCODER_RIGHT))			// L=1, R=1
	{
		_delay_ms(1);
		if((ENCODER_PIN & ENCODER_LEFT) && (ENCODER_PIN & ENCODER_RIGHT))
		{
			encoder_state |= ENCODER_LAST_STATE;							// текущее устойчивое состояние енкодера 1
			encoder_state |= ENCODER_MEASURE;								// разрешаем определения направления вращения енкодера
		}
	}
	

// проверяем кнопку енкодера
	if(!(ENCODER_PIN & ENCODER_BUTTON))
	{
		_delay_ms(1);
		if(!(ENCODER_PIN & ENCODER_BUTTON))
		{
			BUTTON_SET_PORT |= (1<<BUTTON_SET_PIN);								// нажимаем кнопку "SET" 
			_delay_ms(100);
			BUTTON_SET_PORT &=~ (1<<BUTTON_SET_PIN);
		}
	}

	return encoder_counter;
}

//-----------------------------------------------
void HC595_send(uint8_t data)	// передача данных на выводы HC595
{
	HC595_DATA_low();
	HC595_CLK_low();
	HC595_DATA_low();
	
	for(uint8_t i=0; i<8; i++)
	{
		HC595_CLK_low();
		if(data & 0b10000000) HC595_DATA_high()
		else HC595_DATA_low();
		HC595_CLK_high();
		data<<=1;
	}
	HC595_LOAD_high();
	_delay_us(100);
	HC595_LOAD_low();
}//	конец работы с HC595

//-----------------------------------------------
void razbivka_chisla (unsigned int vse_chislo)
{
	razr1 = vse_chislo/1000;					// тысячи
	razr2 = vse_chislo%1000/100;				// сотни
	razr3 = vse_chislo%100/10;					// десятки
	razr4 = vse_chislo%10;						// единицы
	
}

//-----------------------------------------------
ISR (TIMER0_OVF_vect)
{
	if (digit_place == 1)									//включаем 1-й разряд, остальные выключаем
	{
		digit_PORT |=0b11110000;							// сначала очищаем все разряды индикатора
		digit_PORT &= digit1;								// потом зажигаем 1 разряд индикатора
		HC595_send(digits[razr1]);
		
		mili_seconds++;
		if(mili_seconds == 119)								// 119 отстает на 9 секунд за 30 минут
		{
			mili_seconds = 0;
			seconds++;
			if(seconds == 60)
			{
				seconds = 0;
				minutes++;
				if(minutes == 60)
				{
					minutes = 0;
					hours++;
					if(hours == 99)
						hours = 0;
				}
			}
		}
	}
	if (digit_place == 2)									//включаем 2-й разряд, остальные выключаем
	{
		digit_PORT |=0b11110000;							// сначала очищаем все разряды индикатора
		digit_PORT &= digit2;								// потом зажигаем 2 разряд индикатора
		HC595_send(digits[razr2]);
	}
	if (digit_place == 3)									//включаем 3-й разряд, остальные выключаем
	{
		digit_PORT |=0b11110000;							// сначала очищаем все разряды индикатора
		digit_PORT &= digit3;								// потом зажигаем 3 разряд индикатора
		HC595_send(digits[razr3]);
	}
	if (digit_place == 4)									//включаем 4-й разряд, остальные выключаем
	{
		digit_PORT |=0b11110000;							// сначала очищаем все разряды индикатора
		digit_PORT &= digit4;								// потом зажигаем 4 разряд индикатора
		HC595_send(digits[razr4]);
	}
		
	digit_place++;											// добавляем к переменной digit_place единицу
	if (digit_place > 4)
	digit_place = 1;									// отслеживаем переменную digit_place, чтобы она не превысила значение 4
}


//***************************************************************************************************************************************************
int main(void)
{
	// настраиваем порты
	DDRB  = 0b11111111;
	PORTB = 0b11110000;
	DDRC  = 0b00011100;
	PORTC = 0b00000000;
	DDRD  = 0b01000000;
	PORTD = 0b00000000;
	
	HC595_send(digits[10]);
		
	// Настройка 0-го таймер счетчика на прерывание по переполнению
	TCCR0 = (0<<CS02)|(1<<CS01)|(1<<CS00);														// f/64
	TIMSK = (0<<OCIE2)|(0<<TOIE2)|(0<<TICIE1)|(0<<OCIE1A)|(0<<OCIE1B)|(0<<TOIE1)|(1<<TOIE0);	// interrupt overflow Timer0
	TCNT0 = 0;
	sei ();
	
	RELE_POWER_ON();					// подаем питание на блок питание 36 вольт
	led_on();							// включаем подсветку на кнопке питания


	
//*****************************************************************************************************************
// основной цикл программы
while(1)
{
	//count = seconds;
	count = hours*100 + minutes;
	razbivka_chisla(count);
		
// проверяем кнопку питания
	if(!button_power)
	{
		_delay_ms(3);																		// устраняем дребезг контактов
		if(!button_power)
		{
			BUTTON_ON_OFF_PORT |= (1<<BUTTON_ON_OFF_PIN);									// нажимаем кнопку "ON/OFF"
			_delay_ms(100);
			BUTTON_ON_OFF_PORT &=~ (1<<BUTTON_ON_OFF_PIN);
			_delay_ms(1000);
		}
	}

	encoder();

}	// конец вечного цикла END PROGRAMM
	
	
	
}