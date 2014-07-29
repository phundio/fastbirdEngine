#pragma once
#include "Vec3.h"

namespace fastbird
{
	class AABB
	{
	public:
		AABB()
		{
			Invalidate();
		}

		bool IsValid()
		{
			return mMax >= mMin;
		}

		void Invalidate()
		{
			mMin = Vec3::MAX;
			mMax = Vec3::MIN;
		}

		void Merge(const Vec3& point)
		{
			mMin.KeepLesser(point);
			mMax.KeepGreater(point);
		}

		const Vec3& GetMin() const { return mMin; }
		const Vec3& GetMax() const { return mMax; }

		void SetMin(const Vec3& min) { mMin = min;}
		void SetMax(const Vec3& max) { mMax = max;}

		Vec3 GetCenter() { return (mMin + mMax) * .5f; }

		void Translate(const Vec3& pos)
		{
			mMin += pos;
			mMax += pos;
		}


	private:
		Vec3 mMin;
		Vec3 mMax;
	};
}