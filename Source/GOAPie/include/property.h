#pragma once

#include <set>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>
#include <limits>

#include "common.h"
#include "glm/glm.hpp"

namespace gie
{
	class Property
	{
		Guid _guid{ NullGuid };
		Guid _owner{ NullGuid };
		StringHash _name{ InvalidStringHash };

	public:
		Property( Guid guid, Guid owner, StringHash hash ) : _guid( guid ), _owner( owner ), _name( hash ) { }
		Property( Guid owner, StringHash hash ) : _guid( randGuid() ), _owner( owner ), _name( hash ) { }
		Property() = delete;
		Property( const Property& ) noexcept = default;
		Property( Property&& ) noexcept = default;
		~Property() = default;

		typedef std::vector< bool >			BooleanVector;
		typedef std::vector< float >		FloatVector;
		typedef std::vector< int32_t >		IntegerVector;
		typedef std::vector< Guid >			GuidVector;
		typedef std::vector< StringHash >	StringHashVector;
		typedef std::vector< glm::vec3 >	Vec3Vector;

		typedef std::variant<
			bool,
			BooleanVector,
			float,
			FloatVector,
			int32_t,
			IntegerVector,
			Guid,
			GuidVector,
			glm::vec3,
			Vec3Vector > Variant;

		Variant value;

		enum Type : uint8_t
		{
			Unknow,
			Boolean,
			BooleanArray,
			Float,
			FloatArray,
			Integer,
			IntegerArray,
			GUID,
			GUIDArray,
			Vec3,
			Vec3Array
		};

		// clang-format off

		// @return Type of data being stored in this property.
		Type type() const
		{
			if( std::holds_alternative< bool >			( value ) )	return Boolean;
			if( std::holds_alternative< BooleanVector >	( value ) )	return BooleanArray;
			if( std::holds_alternative< float >			( value ) )	return Float;
			if( std::holds_alternative< FloatVector >	( value ) )	return FloatArray;
			if( std::holds_alternative< int32_t >		( value ) )	return Integer;
			if( std::holds_alternative< IntegerVector >	( value ) )	return IntegerArray;
			if( std::holds_alternative< Guid >			( value ) ) return GUID;
			if( std::holds_alternative< GuidVector >	( value ) )	return GUIDArray;
			if( std::holds_alternative< glm::vec3 >		( value ) )	return Vec3;
			if( std::holds_alternative< Vec3Vector >	( value ) )	return Vec3Array;
			return Unknow;
		};

		const	bool*			getBool()			const	{ return type() == Boolean ?		&std::get< bool >( value ) : nullptr; }
				bool*			getBool()					{ return type() == Boolean ?		&std::get< bool >( value ) : nullptr; }
		const	BooleanVector*	getBooleanArray()	const	{ return type() == BooleanArray ?	&std::get< BooleanVector >( value ) : nullptr; }
				BooleanVector*	getBooleanArray()			{ return type() == BooleanArray ?	&std::get< BooleanVector >( value ) : nullptr; }

		const	float*			getFloat()			const	{ return type() == Float ?			&std::get< float >( value ) : nullptr; }
				float*			getFloat()					{ return type() == Float ?			&std::get< float >( value ) : nullptr; }
		const	FloatVector*	getFloatArray()		const	{ return type() == FloatArray ?		&std::get< FloatVector >( value ) : nullptr; }
				FloatVector*	getFloatArray()				{ return type() == FloatArray ?		&std::get< FloatVector >( value ) : nullptr; }

		const	int32_t*		getInteger()		const	{ return type() == Integer ?		&std::get< int32_t >( value ) : nullptr; }
				int32_t*		getInteger()				{ return type() == Integer ?		&std::get< int32_t >( value ) : nullptr; }
		const	IntegerVector*	getIntegerArray()	const	{ return type() == IntegerArray ?	&std::get< IntegerVector >( value ) : nullptr; }
				IntegerVector*	getIntegerArray()			{ return type() == IntegerArray ?	&std::get< IntegerVector >( value ) : nullptr; }

		const	Guid*			getGuid()			const	{ return type() == GUID ?			&std::get< Guid >( value ) : nullptr; }
				Guid*			getGuid()					{ return type() == GUID ?			&std::get< Guid >( value ) : nullptr; }
		const	GuidVector*		getGuidArray()		const	{ return type() == GUIDArray ?		&std::get< GuidVector >( value ) : nullptr; }
				GuidVector*		getGuidArray()				{ return type() == GUIDArray ?		&std::get< GuidVector >( value ) : nullptr; }

		const	StringHash*			getStringHash()			const	{ return type() == GUID ?		&std::get< Guid >( value ) : nullptr; }
				StringHash*			getStringHash()					{ return type() == GUID ?		&std::get< Guid >( value ) : nullptr; }
		const	StringHashVector*	getStringHashArray()	const	{ return type() == GUIDArray ?	reinterpret_cast< const StringHashVector* >( &std::get< GuidVector >( value ) ) : nullptr; }
				StringHashVector*	getStringHashArray()			{ return type() == GUIDArray ?	reinterpret_cast<		StringHashVector* >( &std::get< GuidVector >( value ) ) : nullptr; }

		const	glm::vec3*		getVec3()			const	{ return type() == Vec3 ?			&std::get< glm::vec3 >( value ) : nullptr; }
				glm::vec3*		getVec3()					{ return type() == Vec3 ?			&std::get< glm::vec3 >( value ) : nullptr; }
		const	Vec3Vector*		getVec3Array()		const	{ return type() == Vec3Array ?		&std::get< Vec3Vector >( value ) : nullptr; }
				Vec3Vector*		getVec3Array()				{ return type() == Vec3Array ?		&std::get< Vec3Vector >( value ) : nullptr; }

		// clang-format on

		// @return Guid for this property.
		Guid guid() const { return _guid; };

		// @return Guid for data entity which this property belongs to.
		Guid ownerGuid() const { return _owner; }

		// @return StringHash representing the name given to this property.
		StringHash hash() const { return _name; }

        // Returns a string representation of the value stored in the variant.
		std::string toString() const
		{
			struct Visitor
			{
				std::string operator()( bool v ) const
				{
					return v ? "true" : "false";
				}
				std::string operator()( const BooleanVector& v ) const
				{
					std::string result = "[";
					for( size_t i = 0; i < v.size(); ++i )
					{
						result += v[ i ] ? "true" : "false";
						if( i < v.size() - 1 )
							result += ", ";
					}
					result += "]";
					return result;
				}
				std::string operator()( float v ) const
				{
					if( v == std::numeric_limits< float >::max() )
					{
						return "MAX";
					}
					return std::to_string( v );
				}
				std::string operator()( const FloatVector& v ) const
				{
					std::string result = "[";
					for( size_t i = 0; i < v.size(); ++i )
					{
						const float value = v[ i ];
						const bool isMax = value == std::numeric_limits< float >::max();
						result += isMax ? "MAX" : std::to_string( value );
						if( i < v.size() - 1 )
							result += ", ";
					}
					result += "]";
					return result;
				}
				std::string operator()( int32_t v ) const
				{
					return std::to_string( v );
				}
				std::string operator()( const IntegerVector& v ) const
				{
					std::string result = "[";
					for( size_t i = 0; i < v.size(); ++i )
					{
						result += std::to_string( v[ i ] );
						if( i < v.size() - 1 )
							result += ", ";
					}
					result += "]";
					return result;
				}
				std::string operator()( const Guid& v ) const
				{
					auto strView = stringRegister().get( v );
					return strView.empty() ? std::to_string( v ) : std::string{ strView };
				}
				std::string operator()( const GuidVector& v ) const
				{
					std::string result = "[";
					for( size_t i = 0; i < v.size(); ++i )
					{
						Guid guid = v[ i ];
						auto strView = stringRegister().get( guid );
						result += strView.empty() ? std::to_string( guid ) : strView;
						if( i < v.size() - 1 )
							result += ", ";
					}
					result += "]";
					return result;
				}
				std::string operator()( const glm::vec3& v ) const
				{
					return "vec3(" + std::to_string( v.x ) + ", " + std::to_string( v.y ) + ", " + std::to_string( v.z ) + ")";
				}
				std::string operator()( const Vec3Vector& v ) const
				{
					std::string result = "[";
					for( size_t i = 0; i < v.size(); ++i )
					{
						result += "vec3(" + std::to_string( v[ i ].x ) + ", " + std::to_string( v[ i ].y ) + ", "
								  + std::to_string( v[ i ].z ) + ")";
						if( i < v.size() - 1 )
							result += ", ";
					}
					result += "]";
					return result;
				}
			};

			return std::visit( Visitor{}, value );
		}
	};

	typedef std::pair< Guid, Property::Variant > Definition;
	typedef std::pair< StringHash, Property::Variant > NamedDefinition;
}