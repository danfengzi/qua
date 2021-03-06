#ifndef _TIME
#define _TIME

#define INFINITE_TICKS	0x7fffffff

#include "Metric.h"

typedef int32	tick_t;

/*
 * space is no longer a premium. TODO XXX FIXME perhaps this is better as a triplet.
 * sometimes it definitely is. that would be more robust for shifting meters.
 */
struct qua_time {
public:
	tick_t		ticks;
	Metric		*metric;
};

class Time: public qua_time
{
public:
	Time(long c=0, Metric *m=&Metric::std);
				
	float SecsValue() const;
	void		SetToSecs(float);
	long		TickValue(Metric *m);
	long		BeatValue();
	long		BarValue();
	char		*StringValue();
	void		GetBBQValue(int &bar, int &barbeat, int &beattick);
	void		Reset();
	void		Set(const long c = 0, Metric *m = &Metric::std);
	void		Set(const long b, const long bt, const long c, Metric *m);
	void		Set(const char *);
	
	void		IncrementBar();
	void		IncrementBeat();
	void		IncrementTick();
	void		DecrementBar();
	void		DecrementBeat();
	void		DecrementTick();
	
	bool operator ! () const;

	bool		operator == (const Time & t2);
	bool		operator != (const Time &t2);

	bool		operator >= (const Time & t2);
	bool		operator <= (const Time &t2);
	bool		operator > (const Time &t2);
	bool		operator < (const Time &t2);
	Time		operator + (const Time& t2);
	Time		operator += (const Time &t2);
	Time		operator - (const Time& t2);
	Time		operator -= (const Time & t2);
	Time		operator % (const Time &t2);
	Time		operator %= (const Time &t2);
	Time		operator & (const Metric &m);
	Time		operator & (Metric *const m);
	Time		operator &= (const Metric &m);
	Time		operator &= (Metric * const m);
	Time		operator / (const int n);
	Time		operator - (const int n);
	Time		operator + (const int n);

	long		operator = (const long n);
	Time		operator ~ ();

	static Time	zero;
	static Time infinity;
			
};

#endif