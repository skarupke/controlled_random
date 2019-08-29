#pragma once

// Copyright Malte Skarupke 2019.
// Distributed under the Boost Software License, Version 1.0.
// (See http://www.boost.org/LICENSE_1_0.txt)

#include <vector>
#include <random>
#include <algorithm>
#include <cassert>

namespace ska
{

template<typename T>
inline T lerp(T a, T b, float t)
{
    T dist = b - a;
    return a + t * dist;
}

inline uint32_t round_positive_float(float f)
{
    return static_cast<uint32_t>(f + 0.5f);
}

template<typename It, typename Compare>
void heap_top_updated(It begin, It end, Compare && compare)
{
    using std::swap;
    std::ptrdiff_t num_items = end - begin;
    for (std::ptrdiff_t current = 0;;)
    {
        std::ptrdiff_t child_to_update = current * 2 + 1;
        if (child_to_update >= num_items)
            break;
        std::ptrdiff_t second_child = child_to_update + 1;
        if (second_child < num_items && compare(begin[child_to_update], begin[second_child]))
            child_to_update = second_child;
        if (!compare(begin[current], begin[child_to_update]))
            break;
        swap(begin[current], begin[child_to_update]);
        current = child_to_update;
    }
}
template<typename It>
void heap_top_updated(It begin, It end)
{
    return heap_top_updated(begin, end, std::less<>());
}

class WeightedDistribution
{
    static constexpr float fixed_point_multiplier = 1024.0f * 1024.0f;
    struct Weight
    {
        Weight(float frequency, size_t original_index)
            : average_time_between_events(round_positive_float(frequency * fixed_point_multiplier))
            , original_index(original_index)
        {
            next_event_time = average_time_between_events;
        }

        uint32_t next_event_time;
        uint32_t average_time_between_events;
        size_t original_index;
    };
    struct CompareByNextTime
    {
        uint32_t reference_point = 0;
        bool operator()(const Weight & l, const Weight & r) const
        {
            return (l.next_event_time - reference_point) > (r.next_event_time - reference_point);
        }
    };

    std::vector<Weight> weights;

public:

    WeightedDistribution()
    {
    }

    WeightedDistribution(std::initializer_list<float> il)
    {
        weights.reserve(il.size());
        for (float w : il)
            add_weight(w);
    }

    // how these values were chosen:
    // min_weight was chosen so that the largest number we add in pick_random
    // can be std::numeric_limits<uint32_t>::max() / 4. that gives us enough
    // space to not have to worry about things wrapping around.
    //
    // max_weight was chosen so that its distribution in pick_random would be
    // std::uniform_int_distribution<uint32_t>(0, 102). the bigger numbers we
    // allow, the smaller the range on that distribution. and then similar
    // numbers start to behave the same. so for example if we allowed numbers
    // up to 32768 then 32000 behaves exactly the same as 32768. (they'd both
    // have 32 as an upper limit) I chose a round number that still allows
    // us to notice 1% differences. meaning if you subtract 1% from the max
    // you will actually have a 1% lower chance of being picked
    //
    // if you run into either limit, just grow your range in the other
    // direction meaning if you have a lot of large numbers, maybe add some
    // small numbers instead. the only thing that matters is the ratio between
    // the numbers, not the absolute value of the number. so make sure to use
    // the full range. you could also do an automatic normalization step on
    // your inputs to ensure that they use this range.
    //
    // if you really need a bigger range (because one item needs to happen more
    // than ten million times more often than another) consider changing this
    // to use uint64_t instead of uint32_t and double instead of float and then
    // increase the fixed_point_multiplier to 1024.0 * 1024.0 * 1024.0 * 1024.0
    static constexpr float min_weight = 1.0f / 1024.0f;
    static constexpr float max_weight = 10240.0f;

    void add_weight(float w)
    {
        // since I'm using fixed point math, I only support a certain range
        assert(w >= min_weight);
        assert(w <= max_weight);
        weights.emplace_back(1.0f / w, weights.size());
    }

    size_t num_weights() const
    {
        return weights.size();
    }

    // you need to call this once after adding all the weights to this
    // WeightedDistribution otherwise the first couple of picks will always
    // be deterministic.
    template<typename Random>
    void initialize_randomness(Random & randomness)
    {
        for (Weight & w : weights)
        {
            w.next_event_time = std::uniform_int_distribution<uint32_t>(0, w.average_time_between_events)(randomness);
        }
        std::make_heap(weights.begin(), weights.end(), CompareByNextTime{0});
    }

    // use this to pick a random item. it will give the distribution that you
    // asked for but try to not repeat the same item too often, or to let too
    // much time pass since an item was picked before it gets picked again.
    template<typename Random>
    size_t pick_random(Random & randomness)
    {
        Weight & picked = weights.front();
        size_t result = picked.original_index;
        uint32_t reference_point = picked.next_event_time;
        uint32_t to_add = std::uniform_int_distribution<uint32_t>(0, picked.average_time_between_events)(randomness);
        // uncomment these three lines to blend in 25% determinism
        //to_add *= 3;
        //to_add /= 4;
        //to_add += picked.average_time_between_events / 4;
        picked.next_event_time += to_add;
        heap_top_updated(weights.begin(), weights.end(), CompareByNextTime{reference_point});
        return result;
    }
};

// this is a version of the above for when you just have two choicse:
// just success and fail. so a single chance value is enough and you
// don't need weights. it's a bit slow though. I wouldn't use it as is.
// just keeping it in here if you need this. to make it faster, probably
// wanna switch it to use std::uniform_int_distribution instead of
// std::uniform_real_distribution, like the above class
/*template<typename Randomness>
bool random_success(float chance, float & success_state, float & fail_state, Randomness & randomness)
{
    float success_frequency = 1.0f / chance;
    float fail_frequency = 1.0f / (1.0f - chance);
    if (success_state < fail_state)
    {
        fail_state -= success_state;
        success_state = std::uniform_real_distribution<float>(0.0f, success_frequency)(randomness);
        return true;
    }
    else
    {
        success_state -= fail_state;
        fail_state = std::uniform_real_distribution<float>(0.0f, fail_frequency)(randomness);
        return false;
    }
}*/

class ControlledRandom
{
    float state = 1.0f;
    uint32_t index = 0;
    static constexpr const float constant_to_multiply[101] =
    {
        1.0f,
        0.999842823f, 0.999372184f, 0.99858737f, 0.997489989f, 0.996079504f, // 5%
        0.994353354f, 0.992320299f, 0.989976823f, 0.987323165f, 0.984358072f, // 10%
        0.98108995f, 0.977510273f, 0.973632514f, 0.969447076f, 0.964966297f, // 15%
        0.960183799f, 0.955135703f, 0.949759007f, 0.94411546f, 0.93817538f, // 20%
        0.931944132f, 0.925439596f, 0.918646991f, 0.91158092f, 0.904245615f, // 25%
        0.896643937f, 0.888772905f, 0.880638301f, 0.872264326f, 0.863632858f, // 30%
        0.854712844f, 0.845594227f, 0.836190343f, 0.826578021f, 0.816753447f, // 35%
        0.806658566f, 0.796402514f, 0.785905063f, 0.775190175f, 0.764275074f, // 40%
        0.753200769f, 0.741862416f, 0.730398834f, 0.71871227f, 0.706894219f, // 45%
        0.694856822f, 0.68264246f, 0.670327544f, 0.657848954f, 0.645235062f, // 50%
        0.6324597f, 0.619563162f, 0.606526911f, 0.593426645f, 0.580169916f, // 55%
        0.566839218f, 0.553292334f, 0.539853752f, 0.526208699f, 0.512536764f, // 60%
        0.498813927f, 0.485045046f, 0.471181333f, 0.457302243f, 0.443413943f, // 65%
        0.429503262f, 0.415506482f, 0.401567012f, 0.38765198f, 0.373695225f, // 70%
        0.359745115f, 0.345868856f, 0.331981093f, 0.31815201f, 0.304365695f, // 75%
        0.290644556f, 0.277024776f, 0.263462812f, 0.249986023f, 0.236542806f, // 80%
        0.223382816f, 0.210130796f, 0.197115764f, 0.184175551f, 0.171426639f, // 85%
        0.158810839f, 0.146292359f, 0.133954003f, 0.121768393f, 0.109754287f, // 90%
        0.0979399607f, 0.0863209665f, 0.0748278722f, 0.0635780841f, 0.0524956733f, // 95%
        0.0415893458f, 0.0308760721f, 0.0203953665f, 0.0100950971f, //99%
        -1.0f,
    };
public:
    explicit ControlledRandom(float odds)
    {
        if (odds <= 0.0f)
            index = 0;
        else if (odds >= 1.0f)
            index = 100;
        else
            index = std::min(std::max(round_positive_float(odds * 100.0f), 1u), 99u);
    }

    template<typename Randomness>
    bool random_success(Randomness & randomness)
    {
        state *= constant_to_multiply[index];
        if (std::uniform_real_distribution<float>()(randomness) <= state)
            return false;
        state = 1.0f;
        return true;
    }
};

}

