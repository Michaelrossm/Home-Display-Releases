#pragma once
#include <string>
#include <cstdint>
#include <vector>
namespace hp::ContactProtocol {
struct Value {std::string label,value;};
struct Address {std::string label,street,houseNumber,postalCode,city,region,country;};
struct Birthday {uint16_t year=0;uint8_t month=0,day=0;};
bool validBirthday(Birthday);
bool parseBirthday(const std::string&,Birthday&);
std::string birthdayText(Birthday);
struct Card {
 std::string firstName,lastName;
 std::string uid,name,email,phone,mobile,street,postalCode,city,country,notes,company,jobTitle;
 std::vector<Value> phones,emails;
 std::vector<Address> addresses;
 Birthday birthday;
};
bool parseVcard(const std::string& input,Card& output);
// Bounded UTF-8 vCard 4.0; no photos, binary attachments or unsupported encodings.
bool serializeVcard(const Card&,std::string& output);
std::string plainNotes(const std::string& input,bool html);
}
