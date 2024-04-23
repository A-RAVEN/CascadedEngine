#include <Serialization.h>
#include <CASTL/CAVector.h>
#include <CASTL/CAMap.h>
#include <CASTL/CAUnorderedMap.h>
#include <CASTL/CAString.h>

int main(int argc, char* argv[])
{
	struct TestStruct
	{
		castl::map<castl::string, int> map;
		castl::unordered_map<castl::string, int> map1;
		int a;
	};

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