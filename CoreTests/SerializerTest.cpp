#include <Serialization.h>
#include <Hasher.h>
#include <CASTL/CAVector.h>
#include <CASTL/CAMap.h>
#include <CASTL/CAUnorderedMap.h>
#include <CASTL/CAString.h>
#include <unordered_map>



struct TestStruct1
{
	float aa;
	float bb;
	float cc[5];
	auto operator <=>(const TestStruct1&) const = default;
};


//
//template<typename T>
//struct TypeDescriptor
//{
//};
//
//template<typename T, size_t Offset>
//struct TypeMemberDesc
//{
//	using type = T;
//	static constexpr size_t offset = Offset;
//};


//
//#define PARENS ()
//#define EXPAND(...) EXPAND4(EXPAND4(EXPAND4(EXPAND4(__VA_ARGS__))))
//#define EXPAND4(...) EXPAND3(EXPAND3(EXPAND3(EXPAND3(__VA_ARGS__))))
//#define EXPAND3(...) EXPAND2(EXPAND2(EXPAND2(EXPAND2(__VA_ARGS__))))
//#define EXPAND2(...) EXPAND1(EXPAND1(EXPAND1(EXPAND1(__VA_ARGS__))))
//#define EXPAND1(...) __VA_ARGS__
//
//#define REFLECTION_MEMBER_LIST_REMAINS(Type, ItrMember, ...) ,TypeMemberDesc<decltype(Type::ItrMember), offsetof(Type, ItrMember) >\
//	__VA_OPT__(REFLECTION_MEMBER_LIST_REMAINS_AGAIN PARENS (Type, __VA_ARGS__) )
//#define REFLECTION_MEMBER_LIST_REMAINS_AGAIN() REFLECTION_MEMBER_LIST_REMAINS
//
//#define REFLECTION_MEMBER_LIST_BEGIN(Type, First, ...) TypeMemberDesc<decltype(Type::First), offsetof(Type, First) >\
//	__VA_OPT__(EXPAND(REFLECTION_MEMBER_LIST_REMAINS(Type, __VA_ARGS__)))
//#define REFLECTION(Type, ...)\
//template<>\
//struct TypeDescriptor<Type>\
//{\
//	using type = Type;\
//	using member_tuple_type = std::tuple < __VA_OPT__( REFLECTION_MEMBER_LIST_BEGIN(Type, __VA_ARGS__) ) >;\
//	constexpr static size_t member_count = std::tuple_size_v<member_tuple_type>;\
//};
//
//#define PRIVATE_REFLECTION(Type) friend struct TypeDescriptor<Type>;


class TestStruct2
{
public:
	auto operator <=>(const TestStruct2&) const = default;
	TestStruct2(float a, float b) : aa(a), bb(b) {}
private:
	float aa;
	float bb;
	float cc[5];

	CA_PRIVATE_REFLECTION(TestStruct2);
};

CA_REFLECTION(TestStruct1, aa, bb, cc);
CA_REFLECTION(TestStruct2, aa, bb, cc);
//template<>struct TypeDescriptor<TestStruct1> {
//	using type = TestStruct1; 
//	using member_tuple_type = std::tuple <  TypeMemberDesc<decltype(TestStruct1::aa), ((::size_t)& reinterpret_cast<char const volatile&>((((TestStruct1*)0)->aa))) >, TypeMemberDesc<decltype(TestStruct1::bb)
//		, ((::size_t)& reinterpret_cast<char const volatile&>((((TestStruct1*)0)->bb))) >
//		, TypeMemberDesc<decltype(TestStruct1::cc), ((::size_t)& reinterpret_cast<char const volatile&>((((TestStruct1*)0)->cc))) > >; 
//	constexpr static size_t member_count = std::tuple_size_v<member_tuple_type>;
//};;


template<typename T, size_t index>
constexpr void evaluate_type() requires careflection::has_type_desc<T>
{
	constexpr auto structSize = CATypeDescriptor<T>::member_count;
	using member_tuple_type = CATypeDescriptor<T>::member_tuple_type;
	if constexpr (index < structSize)
	{
		using visitingElementType = std::tuple_element_t<index, member_tuple_type>;
		std::cout << typeid(typename visitingElementType::type).name() << std::endl;
		std::cout << visitingElementType::offset << std::endl;
		evaluate_type<T, index + 1>();
	}
}

int main(int argc, char* argv[])
{
	evaluate_type<TestStruct1, 0>();
	evaluate_type<TestStruct2, 0>();

	struct TestStruct
	{
		castl::map<castl::string, int> map;
		castl::unordered_map<castl::string, int> map1;
		int a;
	};


	std::unordered_map<TestStruct2, int, cahasher::hash<TestStruct2>> tstMap1;
	tstMap1.insert({ {1, 2}, 3 });

	TestStruct1 testStruct1 = { 1, 2 };
	auto getmember_result = careflection::get_member< TestStruct1, decltype(TestStruct1::bb), offsetof(TestStruct1, bb)>(testStruct1);


	TestStruct testStruct = {};
	testStruct.map = {
		{"a", 1},
		{"b", 2},
		{"c", 3}
	};
	testStruct.map1 = {
		{"d", 4},
		{"e", 5},
		{"f", 6}
	};
	testStruct.a = 3;

	castl::map<castl::string, int> tstMap;

	std::unordered_map<castl::string, int, cahasher::hash<castl::string>> tstMap2 = { {"a", 1}, {"b", 2}, {"c", 3} };

	castl::vector<uint8_t> result;
	cacore::serializer<castl::vector<uint8_t>> writer{result};
	writer.serialize(testStruct);

	castl::string testStr = "aaaa";
	testStr[0] = 'a';

	castl::pair<castl::string, int> pr = {"a", 1};
	auto&& [key, value] = pr;

	TestStruct testStruct1{};
	cacore::deserializer<castl::vector<uint8_t>> loader{ result };
	loader.deserialize(testStruct1);

	return 0;
}