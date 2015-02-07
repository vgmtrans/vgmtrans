// Copyright (c) 2002
// Sergey Klimov (kidd@ukr.net)
// WTL Docking windows
//
// This code is provided "as is", with absolutely no warranty expressed
// or implied. Any use is at your own risk.
//
// This code may be used in compiled form in any way you desire. This
// file may be redistributed unmodified by any means PROVIDING it is
// not sold for profit without the authors written consent, and
// providing that this notice and the authors name is included. If
// the source code in  this file is used in any commercial application
// then a simple email woulod be nice.

#ifndef __SSEC_H__
#define __SSEC_H__

#pragma once

#include <cassert>
#include <deque>
#include <algorithm>
#include <functional>
#include <numeric>

namespace ssec{

template <class TPosition,class TDistance=TPosition>
struct bounds_type
{
	typedef TPosition position_t;
	typedef TDistance distance_t;
	typedef bounds_type<position_t,distance_t> bounds_t;

	bounds_type(){}
	bounds_type(position_t l,position_t h)
			:low(l),hi(h)
	{
	}
	distance_t distance() const
	{
		return hi-low;
	}
	bounds_t& operator=(const bounds_t& x)
	{
		hi=x.hi;
		low=x.low;
		return *this;
	}
	position_t bind(position_t pos) const
	{
		if(pos<low)
			pos=low;
		else
		if(pos>hi)
			pos=hi;
		return pos;
	}
	position_t low;
	position_t hi;
};

//uncommenting default value for TPosition yield "fatal error C1001: INTERNAL COMPILER ERROR" I don't know why
template<class T,class TPosition/*=long*/,class TDistance=long,const TDistance TMinDistance=0>
class spraits
{
public:
	typedef TPosition position;
	typedef TDistance distance;
	static distance min_distance(const T& /*x*/)
	{
		return TMinDistance;
	}
};

//SeparatedSection
template<class T,class TTraits >
class ssection
{
	typedef T						separator_t;
	typedef TTraits					traits;

	typedef std::deque<separator_t>	separators_t;
public:
	typedef typename traits::position				position;
	typedef typename traits::distance				distance;
	typedef bounds_type<position,distance>			bounds_t;
	typedef typename separators_t::size_type		size_type;
	typedef typename separators_t::iterator			iterator;
	typedef typename separators_t::reverse_iterator	reverse_iterator;
	typedef typename separators_t::const_iterator	const_iterator;
	typedef typename separators_t::const_reverse_iterator	const_reverse_iterator;
protected:
	static distance add_distance_limit(distance d,const T& x)
	{
		return d+traits::min_distance(x);
	}
	distance distance_limit(const_iterator begin,const_iterator end) const
	{
//		return std::distance(begin,end)*traits::minDistance;
		return std::accumulate(begin,end,0,add_distance_limit);
	}
	template<class T>
	void lshrink(T begin,T end,position rbound,position offset=0)
	{
		while(begin!=end)
		{
			(*begin)+=offset;
			if((*begin)>rbound)
				(*begin)=rbound;
			rbound+=traits::min_distance(*begin);
			++begin;
		}
	}
	template<class T>
	void rshrink(T begin,T end,position lbound,position offset=0)
	{
		while(begin!=end)
		{
			(*begin)+=offset;
			lbound-=traits::min_distance(*begin);
			if((*begin)<lbound)
				(*begin)=lbound;
			++begin;
		}
	}

	inline const double& Min( const double& a, const double& b )
	{
		return b < a ? b : a;
	}
	inline const double& Max( const double& a, const double& b )
	{
		return a < b ? b : a;
	}

	template<class T>
	void scale(T begin,T end,bounds_t bounds,float ratio)
	{
		if(begin!=end)
		{
			double low=bounds.low;
			double offset=low-begin->get_real();
			(*begin)=low;
			bounds.low+=traits::min_distance(*begin);
			bounds.hi-=distance_limit(++begin,end);
			while(begin!=end)
			{
				(*begin)= begin->get_real() + offset;
// pk021110 replaced following line due to truncation error.
// Also CPtrFrame & CWndFrame classes modified to use double m_pos.
//				(*begin)=(double)(bounds.bind(low+position(((*begin)-low)*ratio)));
				(*begin)=Max((double)bounds.low,Min((double)bounds.hi,low+(begin->get_real()-low)*ratio));
				distance d=traits::min_distance(*begin);
				bounds.hi+=d;
				bounds.low=(*begin)+d;
				++begin;
			}
		}
	}
	template<class T>
	void shift(T begin,T end,distance n)
	{
		std::transform(begin,end,begin,std::bind2nd(std::plus<position>(),n));
	}
public:
	ssection()
	{
	}
	ssection(position low,position hi)
		:m_bounds(low,hi)
	{
	}
	position low() const
	{
		return m_bounds.low;
	}
	position hi() const
	{
		return m_bounds.hi;
	}
	iterator begin()
	{
		return m_separators.begin();
	}
	iterator end()
	{
		return m_separators.end();
	}
	reverse_iterator rbegin()
	{
		return m_separators.rbegin();
	}
	reverse_iterator rend()
	{
		return m_separators.rend();
	}

	const_iterator begin() const
	{
		return m_separators.begin();
	}
	const_iterator end() const
	{
		return m_separators.end();
	}
	const_reverse_iterator rbegin() const
	{
		return m_separators.rbegin();
	}
	const_reverse_iterator rend() const
	{
		return m_separators.rend();
	}

	distance distance_limit() const
	{
		return distance_limit(m_separators.begin(),m_separators.end());
	}
	size_type size() const
	{
		return m_separators.size();
	}

	const_iterator locate(position pos) const
	{
		assert(m_bounds.bind(pos)==pos);
		const_iterator i=m_separators.begin();
		if(i!=m_separators.end())
		{
			i=std::lower_bound(i,m_separators.end(),pos+1,std::less<position>());
			--i;
		}
		return i;

	}
	iterator locate(position pos)
	{
		assert(m_bounds.bind(pos)==pos);
		iterator i=m_separators.begin();
		if(i!=m_separators.end())
		{
			i=std::lower_bound(i,m_separators.end(),pos+1,std::less<position>());
			--i;
		}
		return i;
	}

	position get_frame_low(const_iterator i) const
	{
		assert(i!=m_separators.end());
		return (*i);
	}

	position get_frame_hi(const_iterator i) const
	{
		assert(i!=m_separators.end());
		return (++i==m_separators.end()) ? m_bounds.hi : position(*i);
	}

	distance get_frame_size(const_iterator i) const
	{
		return get_frame_hi(i)-get_frame_low(i);
	}
	void get_effective_bounds(const_iterator i,bounds_t& bounds) const
	{
		bounds.low=m_bounds.low+distance_limit(m_separators.begin(),i);
		bounds.hi =m_bounds.hi-distance_limit(i,m_separators.end());
	}

//	iterator insert(iterator i,const T& x,distance length)
//	{
//        position pos=x;
////      assert(m_bounds.bind(pos)==pos);
//
//        distance leftLimit=distance_limit(m_separators.begin(),i);
//        distance rightLimit=distance_limit(i,m_separators.end())/*+traits::min_distance(x)*/;
//
//        bounds_t ef_bounds(m_bounds.low+leftLimit,m_bounds.hi-rightLimit-traits::min_distance(x));
//
//        assert(ef_bounds.low<=ef_bounds.hi);
//        pos=ef_bounds.bind(pos);
//
//        lshrink(m_separators.begin(),i,pos-leftLimit);
//
//        position pos2=pos+length;
//        if(pos2>ef_bounds.hi)
//                pos2=ef_bounds.hi;
//
//        rshrink(m_separators.rbegin(),reverse_iterator(i),pos2+rightLimit);
//
//		i=m_separators.insert(i,x);
//		(*i)=(i!=m_separators.begin()) ? pos : m_bounds.low;
//		return i;
//	}
	iterator insert(iterator i,const T& x,distance length)
	{
        distance leftLimit=distance_limit(m_separators.begin(),i);
        distance rightLimit=distance_limit(i,m_separators.end())/*+traits::min_distance(x)*/;

        bounds_t ef_bounds(m_bounds.low+leftLimit,m_bounds.hi-rightLimit);
        assert(ef_bounds.low<=ef_bounds.hi);

		distance limit=ef_bounds.distance();
		if(length>limit)
			length=limit;
//		assert(length>=traits::min_distance(x));
		position pos=x;
		bounds_t bounds(pos,pos+length);
		if(bounds.low<ef_bounds.low)
		{
			bounds.low=ef_bounds.low;
			bounds.hi=bounds.low+length;
		}
		if(bounds.hi>ef_bounds.hi)
		{
			bounds.hi=ef_bounds.hi;
			bounds.low=bounds.hi-length;
		}
		rshrink(m_separators.rbegin(),reverse_iterator(i),bounds.hi+rightLimit);
        lshrink(m_separators.begin(),i,bounds.low-leftLimit);

		i=m_separators.insert(i,x);
		(*i)=(i!=m_separators.begin()) ? bounds.low : m_bounds.low;
		return i;
	}

    iterator insert(const T& x,distance length)
    {
        position pos=x;
        assert(m_bounds.bind(pos)==pos);
        iterator i=std::lower_bound(m_separators.begin(),m_separators.end(),pos,std::less<position>());
		return insert(i,x,length);
	}
	void set_position(iterator i,position pos)
	{
		distance limit=distance_limit(m_separators.begin(),i);
		assert(pos-limit>=m_bounds.low);
		lshrink(m_separators.begin(),i,pos-limit);


		limit=distance_limit(i,m_separators.end());
		assert(pos+limit<=m_bounds.hi);

		(*i)=pos;
		rshrink(m_separators.rbegin(),reverse_iterator(i),pos+limit);
	}

	void set_bounds(const bounds_t& bounds)
	{
		assert(bounds.distance()>=distance_limit(m_separators.begin(),m_separators.end()));
		distance prevDist=m_bounds.distance();
//		distance offset=bounds.low-m_bounds.low;
		m_bounds=bounds;
		if(prevDist!=0)
		{
			float ratio=float(bounds.distance())/prevDist;
			scale(m_separators.begin(),m_separators.end(),bounds,ratio);
		}
	}
	const_iterator erase(iterator i)
	{
		assert(i!=m_separators.end());
		i=m_separators.erase(i);
		if(i!=m_separators.end() && (i==m_separators.begin()) )
			(*i)=m_bounds.low;
		return i;
	}
	const_iterator replace(iterator i,const T& x)
	{
		assert(i!=m_separators.end());
		position pos=(*i);
		i=m_separators.erase(i);
		i=m_separators.insert(i,x);
		(*i)=pos;
		return i;
	}
//////////////////some ugly additions////////////////////////////////////////
/*
	void set_frame_size(iterator i,distance length)
	{
		assert(i!=m_separators.end());
		assert(m_bounds.distance()>distance_limit(m_separators.begin(),m_separators.end()));
		position pos=(*i);
		iterator inext=i;
		++inext;
		if(length>get_frame_size(i))
		{
			assert(pos<hi()-distance_limit(inext,m_separators.end()));
			distance offset=hi()-distance_limit(inext,m_separators.end())-pos;
			if(offset>length)
				offset=length;
			length-=offset;
			rshrink(m_separators.rbegin(),reverse_iterator(inext),pos+offset);

			if(length>0)
			{
				assert(pos>low()+distance_limit(i,m_separators.end()));
				distance offset=pos-low()+distance_limit(i,m_separators.end());
				if(offset>length)
					offset=length;
				lshrink(m_separators.begin(),inext,pos-offset);
			}
		}
		else
		{
			if(inext!=m_separators.end())
				(*inext)=pos+length;
			else
			{
				distance limit=distance_limit(m_separators.begin(),i);
				if(limit<length)
					length=limit;
				(*i)=hi()-length;
			}
		}
	}
*/
	template<class P>
	void set_bounds(const bounds_t& bounds,P p)
	{
		iterator idecl=std::find_if(m_separators.begin(),m_separators.end(),p);
		assert(idecl!=m_separators.end());

		distance prevDist=m_bounds.distance();
		m_bounds=bounds;
		if(prevDist!=0)
		{
			distance diff=m_bounds.distance()-prevDist;
			if(diff<0)
			{
				distance limit=-(get_frame_size(idecl)-traits::min_distance(*idecl));
				if(diff<limit)
					diff=limit;
			}
			shift(++idecl,m_separators.end(),diff);
			float ratio=float(bounds.distance())/(prevDist+diff);
			scale(m_separators.begin(),m_separators.end(),bounds,ratio);
		}
	}
	template<class P>
	iterator insert(iterator i,P p,const T& x,distance length)
	{
		assert(std::find_if(m_separators.begin(),m_separators.end(),p)!=m_separators.end());

		iterator first=std::find_if(m_separators.begin(),i,p);
		iterator last=i;
		distance n=length/*+traits::min_distance(x)*/;
		if(first==i)
		{
			last=std::find_if(i,m_separators.end(),p);
			distance limit=get_frame_size(last)-traits::min_distance(*last);
			if(n>limit)
				n=limit;
			++last;
		}
		else
		{
			distance limit=get_frame_size(first)-traits::min_distance(*first);
			if(n>limit)
				n=limit;
			n=-n;
			++first;
		}
		shift(first,last,n);
		return insert(i,x,length);
	}

	template<class T>
	iterator erase(iterator i,T p)
	{
		assert(i!=m_separators.end());
		assert(std::find_if(m_separators.begin(),m_separators.end(),p)!=m_separators.end());

		distance n=get_frame_size(i);
		i=m_separators.erase(i);
		iterator first=std::find_if(m_separators.begin(),i,p);
		iterator last=i;
		if(first==i)
		{
			last=std::find_if(i,m_separators.end(),p);
			++last;
			n=-n;
		}
		else
			++first;
		shift(first,last,n);
		return i;
	}
protected:
	separators_t	m_separators;
	bounds_t		m_bounds;
};

//helpers
template<class T,class Pr,class Sz>
T search_n(T begin,T end,Pr pr,Sz n)
{
  while((begin!=end)&&(n!=0))
  {
    if(pr(*begin))
				break;
    ++begin;
    --n;
  }
  return begin;
}
}//namespace ssec
#endif // __SSEC_H__
