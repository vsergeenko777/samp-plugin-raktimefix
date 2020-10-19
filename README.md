# Плагин raktimefix
[Скачать скомпилированную версию](https://github.com/vsergeenko777/samp-plugin-raktimefix/releases). **Совместимо только с SA-MP 0.3.7-R2 Linux сервером**.

## Небольшое объяснение
Этот плагин исправляет потокобезопасность функций RakNet::GetTime и RakNet::GetTimeNS, которые используют общую глобальную переменную для хранения результата вызова gettimeofday. Вот [код](https://gitlab.com/ziggi/RakSAMP/-/blob/master/raknet/GetTime.cpp) этих функций:

```cpp
static bool initialized=false;
#ifdef _WIN32
static LARGE_INTEGER yo;
#else
static timeval tp, initialTime; // <=== глобальная переменная tp, используемая в функциях GetTime и GetTimeNS
#endif

RakNetTime RakNet::GetTime( void )
{
	if ( initialized == false )
	{
#ifdef _WIN32
		QueryPerformanceFrequency( &yo );
		// The original code shifted right 10 bits
		//counts = yo.QuadPart >> 10;
		// It gives the wrong value since 2^10 is not 1000
	//	counts = yo.QuadPart;// / 1000;
#else
		gettimeofday( &initialTime, 0 );
#endif
		
		initialized = true;
	}
	
#ifdef _WIN32
	LARGE_INTEGER PerfVal;
	
	QueryPerformanceCounter( &PerfVal );
	
	return (RakNetTime)(PerfVal.QuadPart*1000 / yo.QuadPart);
#else
	gettimeofday( &tp, 0 );
	
	// Seconds to ms and microseconds to ms
	return ( tp.tv_sec - initialTime.tv_sec ) * 1000 + ( tp.tv_usec - initialTime.tv_usec ) / 1000;
	
#endif
}


RakNetTimeNS RakNet::GetTimeNS( void )
{
	if ( initialized == false )
	{
#ifdef _WIN32
		QueryPerformanceFrequency( &yo );
		// The original code shifted right 10 bits
		//counts = yo.QuadPart >> 10;
		// It gives the wrong value since 2^10 is not 1000
		//	counts = yo.QuadPart;// / 1000;
#else
		gettimeofday( &initialTime, 0 );
#endif

		initialized = true;
	}

#ifdef _WIN32
	LARGE_INTEGER PerfVal;

	QueryPerformanceCounter( &PerfVal );

	__int64 quotient, remainder;
	quotient=((PerfVal.QuadPart*1000) / yo.QuadPart);
	remainder=((PerfVal.QuadPart*1000) % yo.QuadPart);
	//return (PerfVal.QuadPart*1000 / (yo.QuadPart/1000));
	return quotient*1000 + (remainder*1000 / yo.QuadPart);

#else
	gettimeofday( &tp, 0 );

	return ( tp.tv_sec - initialTime.tv_sec ) * (RakNetTimeNS) 1000000 + ( tp.tv_usec - initialTime.tv_usec );

#endif
}

```
Изначально эти функции вызывались в основном из сетевого потока RakNet (хотя есть места, где это происходит в потоке пользовательского приложения), но на последних версиях SA-MP использует эти функции из своего главного потока в огромных количествах.

Одновременное выполнение любой из этих функций из разных потоков приводит к повреждению глобальной переменной tp, что в итоге сказывается на возвращаемом результате функции. И чем больше вызовов совершается из разных потоков, тем больше вероятность воспроизведения проблемы.

Примечательно, что Win32 реализация этих функций не имеет такого недостатка, результат вызова QueryPerformanceCounter записывается в локальную переменную PerfVal.

### Как проявляется
Последствий от этой ошибки может быть множество, но самая заметная проблема заключается в отправке подтверждений полученных пакетов:
```cpp
unsigned ReliabilityLayer::GenerateDatagram(...)
{
	// ...
  
	if (time > nextAckTime)
	{
		if (acknowlegements.Size()>0)
		{
			output->Write(true);
			messagesSent++;
			statistics.acknowlegementBitsSent +=acknowlegements.Serialize(output, (MTUSize-UDP_HEADER_SIZE)*8-1, true);
			if (acknowlegements.Size()==0)
				nextAckTime=time+(RakNetTimeNS)(ping*(RakNetTime)(PING_MULTIPLIER_TO_RESEND/4.0f));
			else
			{
			//	printf("Ack full\n");
			}

			writeFalseToHeader=false;
		}
		else
		{
			writeFalseToHeader=true;
			nextAckTime=time+(RakNetTimeNS)(ping*(RakNetTime)(PING_MULTIPLIER_TO_RESEND/4.0f));
		}
	}
	else
		writeFalseToHeader=true;
  
	// ...
}
```
RakNet использует полученное функцией RakNet::GetTimeNS время, чтобы запланировать следующую отправку подтверждений клиенту, как это видно на приведенном сверху [коде](https://gitlab.com/ziggi/RakSAMP/-/blob/master/raknet/ReliabilityLayer.cpp#L1265). В ситуациях, когда RakNet::GetTimeNS возвращает некорректный результат (чаще всего он намного больше корректного времени), следующая отправка подтверждений будет запланирована на время в далеком будущем (в рамках секунд). Сервер перестает отправлять подтверждения, что в конечном итоге приводит к решению клиента разорвать соединение.

### Как исправляется
Плагин перехватывает функции RakNet::GetTime и RakNet::GetTimeNS, чтобы использовать свой вариант реализации, который использует локальную переменную для хранения результата gettimeofday:
```cpp
RakNetTime HOOK_RakNet_GetTime()
{
	static bool& initialized = *reinterpret_cast<bool*>(0x81A19C4);
	static timeval& initialTime = *reinterpret_cast<timeval*>(0x81A19BC);

	if (!initialized)
	{
		gettimeofday(&initialTime, NULL);
		initialized = true;
	}

	struct timeval tv; // <=== локальная переменная
	gettimeofday(&tv, NULL);
	return 1000 * (tv.tv_sec - initialTime.tv_sec) + (tv.tv_usec - initialTime.tv_usec) / 1000;
}
```
Это исправляет проблему и делает эти функции безопасными для вызова из нескольких потоков.

### Кто виноват
Очевидно, что разработчики или мейнтейнеры той версии RakNet, которая по сей день используется в SA-MP. Хотя Kalcor тоже внес свой вклад в появление этой проблемы и возможно без него она не была бы такой заметной. Последняя [версия RakNet](https://github.com/facebookarchive/RakNet/blob/master/Source/GetTime.cpp#L183) не имеет этой проблемы, как и более стабильный и функциональный [форк SLikeNet](https://github.com/SLikeSoft/SLikeNet/blob/master/Source/src/GetTime.cpp#L188).

### Но зачем это всё
Долгое время эта проблема оставалась незамеченной из-за того, что на актуальных несколько лет назад дистрибутивах Linux реализация gettimeofday каким-то образом избегала проблемы с повреждением глобальной переменной tp. Проблема воспроизводится лишь в современных дистрибутивах (как пример Debian 9, 10 и более поздние версии), что делает невозможным их использование для размещения SA-MP сервера.

Проблема имеет настолько плавающий характер, что может воспроизводиться только несколько раз в сутки, при этом из-за реализации использования RakTime::GetTimeNS (значение времени сохраняется один раз и [используется](https://gitlab.com/ziggi/RakSAMP/-/blob/master/raknet/RakPeer.cpp#L4349) для всех вызовов ReliabilityLayer::Update) одновременно вылетают абсолютно все игроки. Я смог поймать стабильное воспроизведение проблемы с помощью sleep 0 в server.cfg, при котором TPS (кол-во итераций циклов в секунду) было в районе 400 тысяч тиков в секунду, что резко повысило шансы на проявление проблемы (раз в 10-60 секунд) и поэтому стало возможно её исследовать и исправить.

Можно предположить, что до какой-то версии ядра Linux системный вызов gettimeofday имел блокировку, которая обеспечивала защиту от повреждения глобальной переменной tp, но в какой-то момент это было изменено для повышения производительности (хотя я не смог найти подтверждение этому в истории коммитов [репозитория Linux](https://github.com/torvalds/linux)).

Дополнительно стоит сказать, что я не смог воспроизвести эту проблему на нескольких виртуальных машинах, только на своих физических серверах. Поэтому, возможно, виртуальные сервера полностью избегают эту проблему, либо она каким-то образом зависит от конфигураций системы или других факторов.

### И что в итоге
На текущий момент уже завершена долгосрочная поддержка многих дистрибутивов Linux, на которой эта проблема не воспроизводится. Это значит, что эти дистрибутивы постепенно перестают получать обновления программ и утилит, в том числе самые главные - обновления безопасности. Этот плагин позволяет начать использовать новые современные версии дистрибутивов, чтобы размещать на них свои SA-MP сервера.

И хотя я общался с Kalcor насчет этой проблемы, думаю его уже не так сильно будет интересовать её решение и вряд ли у него будет желание выпустить патч сервера с исправлением:

![forum.sa-mp.com is not working](https://dl.vsergeenko.com/samp_forum_shutdown.png)
