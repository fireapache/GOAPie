#pragma once

// GOAPie Persistency - JSON save/load utilities
// This header provides straightforward serialization and deserialization
// for World, EntityTagRegistry and the StringRegistry strings used by the world.
//
// Design goals:
// - Human-readable JSON with explicit fields and comments in code.
// - Robust to versioning: includes a simple formatVersion.
// - No invasive changes to core types (minimal changes elsewhere).
// - No external persistence dependencies; a small JSON utility is embedded below.
// - Handles size_t GUIDs precisely by storing them as decimal strings in JSON.
// - Handles float-based vectors explicitly.
//
// Notes on IDs and hashes:
// - Entity/Tag/String hashes are implementation-defined numeric ids at runtime.
// - We always persist string names (entity name, property name, tag name) so
//   we can rebuild hashes on load even if numeric values change between runs.
// - For property values of type GUID/GUIDArray, we disambiguate between entity
//   references and string-hash references using both the world entity set and
//   the string registry contents at save-time; on load we remap entity GUIDs
//   to freshly created entity GUIDs and recompute string hashes from names.
//
// JSON schema (formatVersion 1):
// {
//   "formatVersion": 1,
//   "strings": ["..."],                   // all strings used by the world
//   "entities": [
//     {

//       "guid": "<size_t>",             // original guid as decimal string (for remapping only)
//       "name": "...",                  // entity name (may be empty)
//       "tags": [ "TagName", ... ],
//       "properties": [
//         {
//           "name": "PropertyName",
//           "type": "Boolean|BooleanArray|Float|FloatArray|Integer|IntegerArray|GUID|GUIDArray|Vec3|Vec3Array",
//           "value": <...>,              // typed value (GUIDs are strings or objects with asEntity/asString)
//           // When type == GUID, one of the following optional helpers may appear:
//           // "asEntity": "<size_t>", // original entity guid (preferred for entity refs)
//           // "asString": "..."       // original string behind the hash (preferred for string-hash refs)
//         }, ...
//       ]
//     }
//   ]
// }

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#ifdef _WIN32
  #define NOMINMAX
  #include <windows.h>
#endif

#include "goapie.h"

namespace gie
{
namespace persistency
{
    // Minimal JSON utilities (parser + writer) -------------------------------
    namespace json
    {
        enum class Type { Null, Object, Array, String, Number, Boolean };

        struct Value;
        using Object = std::unordered_map<std::string, Value>;
        using Array  = std::vector<Value>;

        struct Value
        {
            using Storage = std::variant<std::nullptr_t, Object, Array, std::string, double, bool>;
            Storage data;

            Value() : data( nullptr ) {}
            Value( std::nullptr_t ) : data( nullptr ) {}
            Value( Object o ) : data( std::move( o ) ) {}
            Value( Array a ) : data( std::move( a ) ) {}
            Value( std::string s ) : data( std::move( s ) ) {}
            Value( const char* s ) : data( std::string( s ) ) {}
            Value( double n ) : data( n ) {}
            Value( bool b ) : data( b ) {}

            Type type() const
            {
                switch( data.index() )
                {
                case 0: return Type::Null;
                case 1: return Type::Object;
                case 2: return Type::Array;
                case 3: return Type::String;
                case 4: return Type::Number;
                case 5: return Type::Boolean;
                default: return Type::Null;
                }
            }

            bool isNull() const    { return type() == Type::Null; }
            bool isObject() const  { return type() == Type::Object; }
            bool isArray() const   { return type() == Type::Array; }
            bool isString() const  { return type() == Type::String; }
            bool isNumber() const  { return type() == Type::Number; }
            bool isBoolean() const { return type() == Type::Boolean; }

            Object&       asObject()       { return std::get<Object>( data ); }
            const Object& asObject() const { return std::get<Object>( data ); }
            Array&        asArray()        { return std::get<Array>( data ); }
            const Array&  asArray()  const { return std::get<Array>( data ); }
            std::string&       asString()       { return std::get<std::string>( data ); }
            const std::string& asString() const { return std::get<std::string>( data ); }
            double&       asNumber()       { return std::get<double>( data ); }
            double        asNumber() const { return std::get<double>( data ); }
            bool&         asBoolean()      { return std::get<bool>( data ); }
            bool          asBoolean() const{ return std::get<bool>( data ); }

            // Serialize to JSON string (indent < 0 => compact)
            std::string dump( int indent = -1, char indentChar = ' ' ) const
            {
                std::string out;
                dumpImpl( out, *this, indent, indentChar, 0 );
                return out;
            }

            static Value parse( const std::string& text )
            {
                struct Parser
                {
                    const char* cur{ nullptr };
                    const char* end{ nullptr };

                    explicit Parser( const char* b, const char* e ) : cur( b ), end( e ) {}

                    bool eof() const { return cur >= end; }
                    char peek() const { return eof() ? '\0' : *cur; }
                    char get() { return eof() ? '\0' : *cur++; }
                    void skipWs() { while( !eof() && ( *cur == ' ' || *cur == '\t' || *cur == '\r' || *cur == '\n' ) ) ++cur; }

                    bool match( char c ) { if( peek() == c ) { ++cur; return true; } return false; }

                    std::string parseString()
                    {
                        std::string s;
                        // assume opening '"' already consumed
                        while( !eof() )
                        {
                            char c = get();
                            if( c == '"' ) break;
                            if( c == '\\' )
                            {
                                if( eof() ) break;
                                char esc = get();
                                switch( esc )
                                {
                                case '"': s.push_back( '"' ); break;
                                case '\\': s.push_back( '\\' ); break;
                                case '/': s.push_back( '/' ); break;
                                case 'b': s.push_back( '\b' ); break;
                                case 'f': s.push_back( '\f' ); break;
                                case 'n': s.push_back( '\n' ); break;
                                case 'r': s.push_back( '\r' ); break;
                                case 't': s.push_back( '\t' ); break;
                                case 'u':
                                {
                                    // Minimal \uXXXX handling (ASCII only)
                                    unsigned code = 0;
                                    for( int i = 0; i < 4 && !eof(); ++i )
                                    {
                                        char h = get();
                                        code <<= 4;
                                        if( h >= '0' && h <= '9' ) code |= ( h - '0' );
                                        else if( h >= 'a' && h <= 'f' ) code |= ( 10 + ( h - 'a' ) );
                                        else if( h >= 'A' && h <= 'F' ) code |= ( 10 + ( h - 'A' ) );
                                        else { code = '?'; break; }
                                    }
                                    if( code <= 0x7F ) s.push_back( static_cast<char>( code ) );
                                    else s.push_back( '?' );
                                    break;
                                }
                                default: s.push_back( esc ); break;
                                }
                            }
                            else
                            {
                                s.push_back( c );
                            }
                        }
                        return s;
                    }

                    double parseNumber()
                    {
                        const char* start = cur - 1; // previous char was first number char
                        while( !eof() )
                        {
                            char c = peek();
                            if( ( c >= '0' && c <= '9' ) || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-' )
                                ++cur;
                            else
                                break;
                        }
                        std::string s( start, cur );
                        try { return std::stod( s ); } catch( ... ) { return 0.0; }
                    }

                    Value parseValue()
                    {
                        skipWs();
                        if( eof() ) return Value{};
                        char c = get();
                        if( c == 'n' ) { // null
                            if( ( end - cur ) >= 3 && cur[ 0 ] == 'u' && cur[ 1 ] == 'l' && cur[ 2 ] == 'l' ) { cur += 3; return Value{}; }
                            return Value{};
                        }
                        if( c == 't' ) { // true
                            if( ( end - cur ) >= 3 && cur[ 0 ] == 'r' && cur[ 1 ] == 'u' && cur[ 2 ] == 'e' ) { cur += 3; return Value{ true }; }
                            return Value{ true };
                        }
                        if( c == 'f' ) { // false
                            if( ( end - cur ) >= 4 && cur[ 0 ] == 'a' && cur[ 1 ] == 'l' && cur[ 2 ] == 's' && cur[ 3 ] == 'e' ) { cur += 4; return Value{ false }; }
                            return Value{ false };
                        }
                        if( c == '"' )
                        {
                            return Value{ parseString() };
                        }
                        if( c == '{' )
                        {
                            Object obj;
                            skipWs();
                            if( match( '}' ) ) return Value{ std::move( obj ) };
                            while( !eof() )
                            {
                                skipWs();
                                if( !match( '"' ) ) break; // invalid -> stop
                                std::string key = parseString();
                                skipWs();
                                if( !match( ':' ) ) break;
                                Value val = parseValue();
                                obj.emplace( std::move( key ), std::move( val ) );
                                skipWs();
                                if( match( '}' ) ) break;
                                if( !match( ',' ) ) break;
                            }
                            return Value{ std::move( obj ) };
                        }
                        if( c == '[' )
                        {
                            Array arr;
                            skipWs();
                            if( match( ']' ) ) return Value{ std::move( arr ) };
                            while( !eof() )
                            {
                                Value val = parseValue();
                                arr.emplace_back( std::move( val ) );
                                skipWs();
                                if( match( ']' ) ) break;
                                if( !match( ',' ) ) break;
                            }
                            return Value{ std::move( arr ) };
                        }
                        // number (starts from c)
                        if( ( c >= '0' && c <= '9' ) || c == '-' )
                        {
                            return Value{ parseNumber() };
                        }
                        return Value{};
                    }
                };

                Parser p( text.data(), text.data() + text.size() );
                Value v = p.parseValue();
                return v;
            }

        private:
            static void dumpIndent( std::string& out, int indent, char indentChar, int level )
            {
                if( indent < 0 ) return;
                for( int i = 0; i < level * indent; ++i ) out.push_back( indentChar );
            }

            static void dumpString( std::string& out, const std::string& s )
            {
                out.push_back( '"' );
                for( char c : s )
                {
                    switch( c )
                    {
                    case '"': out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\b': out += "\\b"; break;
                    case '\f': out += "\\f"; break;
                    case '\n': out += "\\n"; break;
                    case '\r': out += "\\r"; break;
                    case '\t': out += "\\t"; break;
                    default:
                        if( static_cast<unsigned char>( c ) < 0x20 )
                        {
                            char buf[7];
                            std::snprintf( buf, sizeof( buf ), "\\u%04x", (int)(unsigned char)c );
                            out += buf;
                        }
                        else out.push_back( c );
                        break;
                    }
                }
                out.push_back( '"' );
            }

            static void dumpImpl( std::string& out, const Value& v, int indent, char indentChar, int level )
            {
                switch( v.type() )
                {
                case Type::Null: out += "null"; break;
                case Type::Boolean: out += ( v.asBoolean() ? "true" : "false" ); break;
                case Type::Number:
                {
                    // print integers without .0 when possible
                    double d = v.asNumber();
                    if( std::isfinite( d ) && std::floor( d ) == d ) out += std::to_string( (long long) d );
                    else out += std::to_string( d );
                    break;
                }
                case Type::String:
                    dumpString( out, v.asString() );
                    break;
                case Type::Array:
                {
                    const auto& arr = v.asArray();
                    out.push_back( '[' );
                    if( !arr.empty() )
                    {
                        if( indent >= 0 ) out.push_back( '\n' );
                        for( size_t i = 0; i < arr.size(); ++i )
                        {
                            dumpIndent( out, indent, indentChar, level + 1 );
                            dumpImpl( out, arr[ i ], indent, indentChar, level + 1 );
                            if( i + 1 < arr.size() ) out.push_back( ',' );
                            if( indent >= 0 ) out.push_back( '\n' );
                        }
                        dumpIndent( out, indent, indentChar, level );
                    }
                    out.push_back( ']' );
                    break;
                }
                case Type::Object:
                {
                    const auto& obj = v.asObject();
                    out.push_back( '{' );
                    if( !obj.empty() )
                    {
                        if( indent >= 0 ) out.push_back( '\n' );
                        size_t i = 0;
                        for( const auto& kv : obj )
                        {
                            dumpIndent( out, indent, indentChar, level + 1 );
                            dumpString( out, kv.first );
                            out.push_back( ':' );
                            if( indent >= 0 ) out.push_back( ' ' );
                            dumpImpl( out, kv.second, indent, indentChar, level + 1 );
                            if( ++i < obj.size() ) out.push_back( ',' );
                            if( indent >= 0 ) out.push_back( '\n' );
                        }
                        dumpIndent( out, indent, indentChar, level );
                    }
                    out.push_back( '}' );
                    break;
                }
                }
            }
        };
    } // namespace json

    // Small helpers -----------------------------------------------------------

    inline std::string executableDirectory()
    {
    #ifdef _WIN32
        char path[MAX_PATH] = { 0 };
        DWORD len = GetModuleFileNameA( nullptr, path, MAX_PATH );
        if( len == 0 ) return std::string( "." );
        std::string full( path, path + len );
        // trim filename, keep directory only
        size_t pos = full.find_last_of( "\\/" );
        if( pos != std::string::npos )
            return full.substr( 0, pos );
        else
            return std::string( "." );
    #else
        // Fallback: current directory
        return std::string( "." );
    #endif
    }

    inline std::string joinPath( const std::string& dir, const std::string& file )
    {
        if( dir.empty() ) return file;
        const char sep =
        #ifdef _WIN32
            '\\';
        #else
            '/';
        #endif
        if( dir.back() == sep ) return dir + file;
        return dir + sep + file;
    }

    inline std::string toDecString( size_t value )
    {
        return std::to_string( static_cast< unsigned long long >( value ) );
    }

    inline bool parseDecString( const json::Value& node, size_t& out )
    {
        if( node.isString() )
        {
            const auto& s = node.asString();
            try { out = static_cast<size_t>( std::stoull( s ) ); return true; } catch( ... ) { return false; }
        }
        else if( node.isNumber() )
        {
            // Best-effort fallback if older files used numeric GUIDs
            out = static_cast<size_t>( static_cast<unsigned long long>( node.asNumber() ) );
            return true;
        }
        return false;
    }

    // Map enum type to string for readability
    inline const char* propertyTypeToString( Property::Type type )
    {
        using T = Property::Type;
        switch( type )
        {
        case T::Boolean:      return "Boolean";
        case T::BooleanArray: return "BooleanArray";
        case T::Float:        return "Float";
        case T::FloatArray:   return "FloatArray";
        case T::Integer:      return "Integer";
        case T::IntegerArray: return "IntegerArray";
        case T::GUID:         return "GUID";
        case T::GUIDArray:    return "GUIDArray";
        case T::Vec3:         return "Vec3";
        case T::Vec3Array:    return "Vec3Array";
        default:              return "Unknown";
        }
    }

    inline Property::Type propertyTypeFromString( const std::string& str )
    {
        if( str == "Boolean" )      return Property::Boolean;
        if( str == "BooleanArray" ) return Property::BooleanArray;
        if( str == "Float" )        return Property::Float;
        if( str == "FloatArray" )   return Property::FloatArray;
        if( str == "Integer" )      return Property::Integer;
        if( str == "IntegerArray" ) return Property::IntegerArray;
        if( str == "GUID" )         return Property::GUID;
        if( str == "GUIDArray" )    return Property::GUIDArray;
        if( str == "Vec3" )         return Property::Vec3;
        if( str == "Vec3Array" )    return Property::Vec3Array;
        return Property::Unknow;
    }

    // Serialization helpers ---------------------------------------------------

    inline json::Value makeJsonVec3( const glm::vec3& vec )
    {
        json::Array arr;
        arr.emplace_back( static_cast< double >( vec.x ) );
        arr.emplace_back( static_cast< double >( vec.y ) );
        arr.emplace_back( static_cast< double >( vec.z ) );
        return json::Value{ std::move( arr ) };
    }

    inline bool parseJsonVec3( const json::Value& node, glm::vec3& out )
    {
        if( !node.isArray() ) return false;
        const auto& arr = node.asArray();
        if( arr.size() != 3u || !arr[0].isNumber() || !arr[1].isNumber() || !arr[2].isNumber() ) return false;
        out.x = static_cast<float>( arr[ 0 ].asNumber() );
        out.y = static_cast<float>( arr[ 1 ].asNumber() );
        out.z = static_cast<float>( arr[ 2 ].asNumber() );
        return true;
    }

    inline void collectStringIfAny( const std::string_view text, std::unordered_set<std::string>& out )
    {
        if( !text.empty() ) out.insert( std::string( text ) );
    }

    // Build JSON object for a property, while also collecting used strings.
    inline json::Value serializeProperty(
        const Property& property,
        const Blackboard& worldContext,
        std::unordered_set<std::string>& collectedStrings )
    {
        json::Object prop;

        // Property name from its hash
        const std::string_view propName = stringRegister().get( property.hash() );
        collectStringIfAny( propName, collectedStrings );
        prop[ "name" ] = std::string( propName );
        prop[ "type" ] = propertyTypeToString( property.type() );

        switch( property.type() )
        {
        case Property::Boolean:
            prop[ "value" ] = property.getBool() ? ( *property.getBool() ? true : false ) : false;
            break;
        case Property::BooleanArray:
        {
            json::Array arr;
            if( const auto* vec = property.getBooleanArray() )
            {
                for( bool b : *vec ) arr.emplace_back( b );
            }
            prop[ "value" ] = json::Value{ std::move( arr ) };
            break;
        }
        case Property::Float:
            prop[ "value" ] = property.getFloat() ? static_cast<double>( *property.getFloat() ) : 0.0;
            break;
        case Property::FloatArray:
        {
            json::Array arr;
            if( const auto* vec = property.getFloatArray() )
            {
                for( float f : *vec ) arr.emplace_back( static_cast<double>( f ) );
            }
            prop[ "value" ] = json::Value{ std::move( arr ) };
            break;
        }
        case Property::Integer:
            prop[ "value" ] = property.getInteger() ? static_cast< double >( *property.getInteger() ) : 0.0;
            break;
        case Property::IntegerArray:
        {
            json::Array arr;
            if( const auto* vec = property.getIntegerArray() )
            {
                for( int32_t i : *vec ) arr.emplace_back( static_cast<double>( i ) );
            }
            prop[ "value" ] = json::Value{ std::move( arr ) };
            break;
        }
        case Property::GUID:
        {
            const Guid id = property.getGuid() ? *property.getGuid() : NullGuid;
            // Try to resolve as entity
            const bool refersEntity = worldContext.entity( id ) != nullptr;
            // Try to resolve as string (string-hash)
            const std::string_view maybeStr = stringRegister().get( id );
            if( refersEntity )
            {
                prop[ "value" ] = toDecString( id );
                prop[ "asEntity" ] = toDecString( id );
            }
            else if( !maybeStr.empty() )
            {
                prop[ "value" ] = toDecString( id );
                prop[ "asString" ] = std::string( maybeStr );
                collectStringIfAny( maybeStr, collectedStrings );
            }
            else
            {
                prop[ "value" ] = toDecString( id );
            }
            break;
        }
        case Property::GUIDArray:
        {
            json::Array arr;
            if( const auto* vec = property.getGuidArray() )
            {
                for( Guid id : *vec )
                {
                    const bool refersEntity = worldContext.entity( id ) != nullptr;
                    if( refersEntity )
                    {
                        json::Object e; e[ "asEntity" ] = toDecString( id );
                        arr.emplace_back( std::move( e ) );
                    }
                    else
                    {
                        const std::string_view maybeStr = stringRegister().get( id );
                        if( !maybeStr.empty() )
                        {
                            json::Object s; s[ "asString" ] = std::string( maybeStr );
                            arr.emplace_back( std::move( s ) );
                            collectStringIfAny( maybeStr, collectedStrings );
                        }
                        else
                        {
                            arr.emplace_back( toDecString( id ) );
                        }
                    }
                }
            }
            prop[ "value" ] = json::Value{ std::move( arr ) };
            break;
        }
        case Property::Vec3:
            prop[ "value" ] = makeJsonVec3( *property.getVec3() );
            break;
        case Property::Vec3Array:
        {
            json::Array arr;
            if( const auto* vec = property.getVec3Array() )
            {
                for( const auto& v : *vec ) arr.emplace_back( makeJsonVec3( v ) );
            }
            prop[ "value" ] = json::Value{ std::move( arr ) };
            break;
        }
        default:
            // Unknown - skip value
            break;
        }

        return json::Value{ std::move( prop ) };
    }

    inline json::Value serializeEntity( const Entity& entity, const Blackboard& worldContext, std::unordered_set<std::string>& collectedStrings )
    {
        json::Object obj;

        // Name
        const std::string_view entityName = entity.nameHash() != InvalidStringHash ? stringRegister().get( entity.nameHash() ) : std::string_view{};
        collectStringIfAny( entityName, collectedStrings );

        obj[ "guid" ] = toDecString( entity.guid() );
        obj[ "name" ] = std::string( entityName );

        // Tags as names
        json::Array tagsArray;
        for( Tag tagId : entity.tags() )
        {
            const std::string_view tagName = stringRegister().get( tagId );
            if( !tagName.empty() )
            {
                tagsArray.emplace_back( std::string( tagName ) );
                collectedStrings.insert( std::string( tagName ) );
            }
        }
        obj[ "tags" ] = json::Value{ std::move( tagsArray ) };

        // Properties
        json::Array propsArray;
        for( const auto& [ propNameHash, propGuid ] : entity.properties() )
        {
            const Property* property = worldContext.property( propGuid );
            if( !property ) continue;
            auto j = serializeProperty( *property, worldContext, collectedStrings );
            propsArray.emplace_back( std::move( j ) );
            // also collect property name string
            const std::string_view propName = stringRegister().get( propNameHash );
            collectStringIfAny( propName, collectedStrings );
        }
        obj[ "properties" ] = json::Value{ std::move( propsArray ) };

        return json::Value{ std::move( obj ) };
    }

    // Public API -----------------------------------------------------------------

    // Save world context to a JSON file next to executable.
    inline bool SaveWorldToJson( const World& world, const std::string& relativeFilePath )
    {
        const Blackboard& worldContext = world.context();

        // Collect data
        json::Object rootObj;
        rootObj[ "formatVersion" ] = 1.0; // number

        // Entities
        json::Array entitiesArray;
        std::unordered_set<std::string> collectedStrings;
        for( const auto& [ entityGuid, entity ] : worldContext.entities() )
        {
            entitiesArray.emplace_back( serializeEntity( entity, worldContext, collectedStrings ) );
        }
        rootObj[ "entities" ] = json::Value{ std::move( entitiesArray ) };

        // Strings table: add every collected string
        json::Array stringsArray;
        for( const auto& s : collectedStrings ) stringsArray.emplace_back( s );
        rootObj[ "strings" ] = json::Value{ std::move( stringsArray ) };

        // Dump JSON (pretty)
        json::Value root( std::move( rootObj ) );
        std::string jsonText = root.dump( 2 );

        const std::string path = joinPath( executableDirectory(), relativeFilePath );
        std::ofstream out( path, std::ios::binary | std::ios::trunc );
        if( !out.is_open() ) return false;
        out.write( jsonText.data(), static_cast<std::streamsize>( jsonText.size() ) );
        return out.good();
    }

    // Load world context from a JSON file next to executable.
    // This clears current entities/properties/tags and rebuilds them.
    inline bool LoadWorldFromJson( World& world, const std::string& relativeFilePath )
    {
        const std::string path = joinPath( executableDirectory(), relativeFilePath );
        std::ifstream in( path, std::ios::binary );
        if( !in.is_open() ) return false;
        std::ostringstream oss; oss << in.rdbuf();
        const std::string data = oss.str();
        if( data.empty() ) return false;

        json::Value root;
        try
        {
            root = json::Value::parse( data );
        }
        catch( ... )
        {
            return false;
        }
        if( !root.isObject() ) return false;
        const auto& rootObj = root.asObject();

        // version (forward compatible)
        auto itFmt = rootObj.find( "formatVersion" );
        if( itFmt == rootObj.end() || !itFmt->second.isNumber() ) return false;
        const int formatVersion = static_cast<int>( itFmt->second.asNumber() );
        if( formatVersion != 1 ) return false;

        // Load/register strings first so hashing has entries
        auto itStrings = rootObj.find( "strings" );
        if( itStrings != rootObj.end() && itStrings->second.isArray() )
        {
            for( const auto& strNode : itStrings->second.asArray() )
            {
                if( strNode.isString() )
                {
                    stringRegister().add( strNode.asString() );
                }
            }
        }

        // Clear world: entities, properties, tags
        world.context().eraseAll();
        world.context().properties().clear();
        world.context().entityTagRegister().clear();

        // First pass: create entities and build guid map
        std::unordered_map<Guid, Guid> oldToNewGuidMap;
        auto itEntities = rootObj.find( "entities" );
        if( itEntities == rootObj.end() || !itEntities->second.isArray() )
        {
            // nothing to load
            return true;
        }
        const auto& entitiesArray = itEntities->second.asArray();

        struct PendingEntity
        {
            Guid oldGuid{ NullGuid };
            std::string name;
            std::vector<std::string> tagNames;
            std::vector<json::Value> properties; // raw json for second pass
        };
        std::vector<PendingEntity> pending;
        pending.reserve( entitiesArray.size() );

        for( const auto& entityNode : entitiesArray )
        {
            if( !entityNode.isObject() ) continue;
            const auto& entityObj = entityNode.asObject();

            // name & guid
            const auto nameIt = entityObj.find( "name" );
            const auto guidIt = entityObj.find( "guid" );
            if( nameIt == entityObj.end() || guidIt == entityObj.end() ) continue;
            const std::string entityName = nameIt->second.isString() ? nameIt->second.asString() : std::string{};
            size_t oldGuidSz = 0; Guid oldGuid = NullGuid;
            if( parseDecString( guidIt->second, oldGuidSz ) ) oldGuid = static_cast<Guid>( oldGuidSz );

            // create entity with name (keeps name hash consistent by string)
            Entity* newEntity = world.createEntity( entityName );
            if( !newEntity ) continue;
            oldToNewGuidMap[ oldGuid ] = newEntity->guid();

            // collect tags
            std::vector<std::string> tagNames;
            const auto tagsIt = entityObj.find( "tags" );
            if( tagsIt != entityObj.end() && tagsIt->second.isArray() )
            {
                for( const auto& tagNode : tagsIt->second.asArray() )
                {
                    if( tagNode.isString() )
                        tagNames.emplace_back( tagNode.asString() );
                }
            }

            // store properties json for second pass
            std::vector<json::Value> props;
            const auto propsIt = entityObj.find( "properties" );
            if( propsIt != entityObj.end() && propsIt->second.isArray() )
            {
                for( const auto& propNode : propsIt->second.asArray() ) props.emplace_back( propNode );
            }

            pending.push_back( PendingEntity{ oldGuid, entityName, std::move( tagNames ), std::move( props ) } );
        }

        // Second pass: apply tags and properties
        for( const auto& pend : pending )
        {
            Entity* entity = world.entity( oldToNewGuidMap[ pend.oldGuid ] );
            if( !entity ) continue;

            // tags -> to Tag ids then register
            if( !pend.tagNames.empty() )
            {
                std::vector<Tag> tagIds; tagIds.reserve( pend.tagNames.size() );
                for( const auto& tagName : pend.tagNames ) tagIds.push_back( stringHasher( tagName ) );
                world.context().entityTagRegister().tag( entity, tagIds );
            }

            // properties
            for( const auto& propNode : pend.properties )
            {
                if( !propNode.isObject() ) continue;
                const auto& propObj = propNode.asObject();
                const auto nameIt = propObj.find( "name" );
                const auto typeIt = propObj.find( "type" );
                const auto valIt  = propObj.find( "value" );
                if( nameIt == propObj.end() || typeIt == propObj.end() || valIt == propObj.end() ) continue;
                const std::string propName = nameIt->second.isString() ? nameIt->second.asString() : std::string{};
                const std::string typeName = typeIt->second.isString() ? typeIt->second.asString() : std::string{};

                const Property::Type propType = propertyTypeFromString( typeName );

                switch( propType )
                {
                case Property::Boolean:
                {
                    bool b = false;
                    if( valIt->second.isBoolean() ) b = valIt->second.asBoolean();
                    entity->createProperty( propName, b );
                    break;
                }
                case Property::BooleanArray:
                {
                    Property::BooleanVector vec;
                    if( valIt->second.isArray() )
                    {
                        for( const auto& bNode : valIt->second.asArray() )
                            vec.push_back( bNode.isBoolean() ? bNode.asBoolean() : false );
                    }
                    entity->createProperty( propName, vec );
                    break;
                }
                case Property::Float:
                {
                    float f = 0.f;
                    if( valIt->second.isNumber() ) f = static_cast<float>( valIt->second.asNumber() );
                    entity->createProperty( propName, f );
                    break;
                }
                case Property::FloatArray:
                {
                    Property::FloatVector vec;
                    if( valIt->second.isArray() )
                    {
                        for( const auto& fNode : valIt->second.asArray() )
                            vec.push_back( static_cast<float>( fNode.asNumber() ) );
                    }
                    entity->createProperty( propName, vec );
                    break;
                }
                case Property::Integer:
                {
                    int32_t i = 0;
                    if( valIt->second.isNumber() ) i = static_cast<int32_t>( static_cast<long long>( valIt->second.asNumber() ) );
                    entity->createProperty( propName, i );
                    break;
                }
                case Property::IntegerArray:
                {
                    Property::IntegerVector vec;
                    if( valIt->second.isArray() )
                    {
                        for( const auto& iNode : valIt->second.asArray() )
                            vec.push_back( static_cast<int32_t>( static_cast<long long>( iNode.asNumber() ) ) );
                    }
                    entity->createProperty( propName, vec );
                    break;
                }
                case Property::GUID:
                {
                    // Prefer explicit helpers over raw value
                    Guid resolved = NullGuid;
                    auto asEntIt = propObj.find( "asEntity" );
                    auto asStrIt = propObj.find( "asString" );
                    if( asEntIt != propObj.end() )
                    {
                        size_t oldSz = 0; if( parseDecString( asEntIt->second, oldSz ) )
                        {
                            const Guid old = static_cast<Guid>( oldSz );
                            auto mapIt = oldToNewGuidMap.find( old );
                            if( mapIt != oldToNewGuidMap.end() ) resolved = mapIt->second;
                        }
                    }
                    else if( asStrIt != propObj.end() && asStrIt->second.isString() )
                    {
                        const std::string s = asStrIt->second.asString();
                        resolved = stringHasher( s ); // new process-specific hash
                    }
                    else
                    {
                        size_t rawSz = 0; if( parseDecString( valIt->second, rawSz ) )
                        {
                            const Guid old = static_cast<Guid>( rawSz );
                            auto mapIt = oldToNewGuidMap.find( old );
                            resolved = mapIt != oldToNewGuidMap.end() ? mapIt->second : old; // best-effort
                        }
                    }
                    entity->createProperty( propName, resolved );
                    break;
                }
                case Property::GUIDArray:
                {
                    Property::GuidVector vec;
                    if( valIt->second.isArray() )
                    {
                        for( const auto& item : valIt->second.asArray() )
                        {
                            if( item.isString() || item.isNumber() )
                            {
                                size_t oldSz = 0; if( parseDecString( item, oldSz ) )
                                {
                                    const Guid old = static_cast<Guid>( oldSz );
                                    auto mapIt = oldToNewGuidMap.find( old );
                                    vec.push_back( mapIt != oldToNewGuidMap.end() ? mapIt->second : old );
                                }
                            }
                            else if( item.isObject() )
                            {
                                const auto& sub = item.asObject();
                                auto ae = sub.find( "asEntity" );
                                auto as = sub.find( "asString" );
                                if( ae != sub.end() )
                                {
                                    size_t oldSz = 0; if( parseDecString( ae->second, oldSz ) )
                                    {
                                        const Guid old = static_cast<Guid>( oldSz );
                                        auto mapIt = oldToNewGuidMap.find( old );
                                        vec.push_back( mapIt != oldToNewGuidMap.end() ? mapIt->second : old );
                                    }
                                }
                                else if( as != sub.end() && as->second.isString() )
                                {
                                    const std::string s = as->second.asString();
                                    vec.push_back( stringHasher( s ) );
                                }
                            }
                        }
                    }
                    entity->createProperty( propName, vec );
                    break;
                }
                case Property::Vec3:
                {
                    glm::vec3 temp{ 0, 0, 0 };
                    if( parseJsonVec3( valIt->second, temp ) )
                    {
                        entity->createProperty( propName, temp );
                    }
                    else
                    {
                        entity->createProperty( propName, glm::vec3{ 0, 0, 0 } );
                    }
                    break;
                }
                case Property::Vec3Array:
                {
                    Property::Vec3Vector vec;
                    if( valIt->second.isArray() )
                    {
                        for( const auto& item : valIt->second.asArray() )
                        {
                            glm::vec3 temp{ 0, 0, 0 };
                            if( parseJsonVec3( item, temp ) ) vec.push_back( temp );
                        }
                    }
                    entity->createProperty( propName, vec );
                    break;
                }
                default:
                    // Unknown property type - skip
                    break;
                }
            }
        }

        return true;
    }
}
}
