#pragma once
#include <iostream>
#include <vector>
namespace fb{
	// Vectors
	template < class T, typename std::enable_if_t < std::is_pod<T>::value >* = nullptr>
	void write(std::ostream& out, const std::vector<T>& data)
	{
		auto size = data.size();
		out.write((char*)&size, sizeof(size));
		for (const auto& d : data){
			out.write((char*)&d, sizeof(d));
		}
	}

	template < class T, typename std::enable_if_t < std::is_pod<T>::value >* = nullptr>
		void read(std::istream& out, std::vector<T>& data)
	{
		size_t size;
		out.read((char*)&size, sizeof(size));
		data.clear();
		data.reserve(size);
		for (size_t i = 0; i < size; ++i){
			data.push_back(T());
			auto& dest = data.back();
			out.read((char*)&dest, sizeof(T));
		}
	}

	template <class T, typename std::enable_if_t < !std::is_pod<T>::value >* = nullptr>
	void write(std::ostream& out, const std::vector<T>& data)
	{
		auto size = data.size();
		out.write((char*)&size, sizeof(size));
		for (const auto& d : data){
			write(out, d);
		}
	}	

	template <class T, typename std::enable_if_t < !std::is_pod<T>::value >* = nullptr>
	void read(std::istream& out, std::vector<T>& data)
	{
		size_t size;
		out.read((char*)&size, sizeof(size));
		data.clear();
		data.reserve(size);
		for (size_t i = 0; i < size; ++i){
			data.push_back(T());
			auto& dest = data.back();
			read(out, dest);			
		}
	}	

	// string
	void write(std::ostream& stream, const std::string& str);
	void read(std::istream& stream, std::string& str);
}