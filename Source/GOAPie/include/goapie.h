#ifndef GOAPIE_LIB
#define GOAPIE_LIB

#include <set>
#include <string>
#include <unordered_map>
#include <variant>

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

	class Entity
	{
		friend class EntityTagRegister;

	protected:
		class World* _world;
		Guid _guid{ NullGuid };
		std::unordered_map< StringHash, Guid > _propertyGuids;
		TagSet _tags;
		StringHash _nameHash{ InvalidStringHash };
		class Blackboard* _context = nullptr;
		
	public:
		Entity() = delete;
		Entity( class World* world, std::string_view name = "", Blackboard* context = nullptr ) noexcept;
		Entity( const Entity& ) noexcept = default;
		Entity( Entity&& ) noexcept = default;
		~Entity() = default;

		Guid guid() const { return _guid; }
		TagSet& tags() { return _tags; }
		const TagSet& tags() const { return _tags; }
		bool hasTag( Tag tag ) const { return _tags.find( tag ) != _tags.end(); }
		const auto& properties() const { return _propertyGuids; }
		auto nameHash() const { return _nameHash; }

		// Register a property in data entity and world using its literal name.
		// @param name: Property contextual name (e.g. "AmmoCount")
		// @return Pointer to property.
		Property* createProperty( std::string_view name )
		{
			return createProperty( stringHasher( name ) );
		}

		template< typename T >
		std::enable_if_t< std::is_convertible_v< T, std::string_view >, Property* >
		createProperty( std::string_view name, T value )
		{
			return createProperty( stringHasher( name ), stringHasher( value ) );
		}

		// Register a property in data entity and world using its literal name.
		// @param name: Property contextual name (e.g. "AmmoCount")
		// @param value: Default value of property
		// @return Pointer to property.
		Property* createProperty( std::string_view name, Property::Variant value )
		{
			return createProperty( stringHasher( name ), value );
		}

		// Register a property in data entity and world using its name hash.
		// @param hash: Hash of property contextual name (e.g. "AmmoCount")
		// @return Pointer to property.
		Property* createProperty( StringHash nameHash );

		// Register a property in data entity and world using its name hash.
		// @param hash: Hash of property contextual name (e.g. "AmmoCount")
		// @param value: Default value of property
		// @return Pointer to property.
		Property* createProperty( StringHash nameHash, Property::Variant value )
		{
			auto ppt = createProperty( nameHash );
			if( ppt )
			{
				ppt->value = value;
			}
			return ppt;
		}

		// Fetch a property registered in this data entity.
		// @param name: Property contextual name (e.g. "AmmoCount")
		// @return Pointer to property.
		Property* property( std::string_view name )
		{
			return property( stringHasher( name ) );
		}

		// Fetch a property registered in this data entity.
		// @param hash: Hash of property contextual name (e.g. "AmmoCount")
		// @return Pointer to property.
		Property* property( StringHash nameHash );

		// Fetch a property registered in this data entity.
		// @param name: Property contextual name (e.g. "AmmoCount")
		// @return Const pointer to property.
		const Property* property( std::string_view name ) const
		{
			return property( stringHasher( name ) );
		}

		// Fetch a property registered in this data entity.
		// @param hash: Hash of property contextual name (e.g. "AmmoCount")
		// @return Const pointer to property.
		const Property* property( StringHash nameHash ) const;

		void removeProperty( StringHash hash ) { _propertyGuids.erase( hash ); }

		class World* world() const { return _world; }

	};

	class IDataEntityManager
	{
	public:
		// @return Pointer to a new agent entity.
		virtual class Agent* createAgent( std::string_view name ) = 0;
		virtual void removeAgent( const Guid guid ) = 0;
		// @return Pointer to a new data entity.
		virtual class Entity* createEntity( std::string_view name = "", Blackboard* context = nullptr ) = 0;
		virtual void removeEntity( const Guid guid ) = 0;
		// @return Pointer to a new (orphan) property.
		// NOTE: it is not assigned to an entity!
		virtual class Property* createProperty( Guid guid, StringHash hash, Guid owner = NullGuid, Property::Variant defaultValue = false ) = 0;
		virtual void removeProperty( const Guid guid ) = 0;
		virtual void eraseAll() = 0;
		// @return Pointer to an existing property.
		virtual Property* property( const Guid guid ) = 0;
		// @return Pointer to an existing property.
		virtual const Property* property( const Guid guid ) const = 0;
		// @return Pointer to an existing data entity.
		virtual Entity* entity( const Guid guid ) = 0;
		// @return Pointer to an existing data entity.
		virtual const Entity* entity( const Guid guid ) const = 0;
		// @return Pointer to an existing agent.
		virtual class Agent* agent( const Guid guid ) = 0;
		// @return Pointer to an existing agent.
		virtual const class Agent* agent( const Guid guid ) const = 0;
	};

    // Keep track of entity tags, grouping entities for tag based queries  
    class EntityTagRegister  
    {  
		friend class Simulation;  

		World* _world{ nullptr };  
		std::unordered_map< Tag, std::set< Guid > > _storage;  
		const EntityTagRegister* _parent{ nullptr }; // Parent property  

		gie::TagSet* _tagSet( Tag entityTag )  
		{  
			auto mapFind = _storage.find( entityTag );  
			if( mapFind != _storage.end() )  
			{  
				return &mapFind->second;  
			}

			gie::TagSet* set = nullptr;

			if( _parent )
			{
				set = _copyTagSet( *_parent, entityTag );
			}
			
			if( !set )
			{
				set = _ensureTagSet( entityTag );
			}

			assert( set && "Tag set not found!" );

			return set;
		}

		gie::TagSet* _ensureTagSet( Tag entityTag )  
		{  
			auto mapFind = _storage.find( entityTag );  
			if( mapFind == _storage.end() )  
			{  
				auto empl = _storage.emplace( entityTag, std::set< Guid >{ } );  
				return empl.second ? &empl.first->second : nullptr;  
			}
			return &mapFind->second;
		}  

		gie::TagSet* _copyTagSet( const EntityTagRegister& sourceRegister, Tag tag )  
		{  
			const gie::TagSet* sourceTagSet = sourceRegister.tagSet( tag );  
			gie::TagSet* targetTagSet = nullptr;
			
			if( sourceTagSet )  
			{  
				targetTagSet = _ensureTagSet( tag );
				*targetTagSet = *sourceTagSet;
			}

			return targetTagSet;
		}  

	public:  
		EntityTagRegister() = delete;  
		EntityTagRegister( World* world, const EntityTagRegister* parent = nullptr )  
			: _world( world ), _parent( parent ) { }  
		EntityTagRegister( const EntityTagRegister& other ) = default;  

		World* world() const { return _world; }  
		const EntityTagRegister* parent() const { return _parent; }  
		void setParent( const EntityTagRegister* parent ) { _parent = parent; }  

		void tag( Entity* entity, std::vector< Tag >& tags )  
		{  
			if( !entity )
			{
				return;
			}

			for( Tag entityTag : tags )  
			{  
				entity->_tags.insert( entityTag );
				if( auto set = _tagSet( entityTag ) )
				{  
					set->insert( entity->guid() );
				}
			}
		}  

		void tag( Entity* entity, std::vector< Tag >&& tags )  
		{  
			tag( entity, tags );  
		}  

		void untag( Entity* entity, std::vector< Tag >& tags )  
		{  
			if( !entity )
			{
				return;
			}

			for( Tag entityTag : tags )  
			{  
				entity->_tags.erase( entityTag );  
				if( auto set = _tagSet( entityTag ) )  
				{  
					set->erase( entity->guid() );  
				}  
			} 
		}  

		void untag( Entity* entity, std::vector< Tag >&& tags )  
		{  
			untag( entity, tags );  
		}  

		const std::set< Guid >* tagSet( std::string_view tagName ) const
		{
			Tag entityTag = stringHasher( tagName );

			auto mapFind = _storage.find( entityTag );
			if( mapFind != _storage.end() )
			{
				return &mapFind->second;
			}

			// Getting from parent if possible
			if( _parent )
			{
				return _parent->tagSet( entityTag );
			}

			return nullptr;
		}

		const std::set< Guid >* tagSet( Tag entityTag ) const
		{
			auto mapFind = _storage.find( entityTag );
			if( mapFind != _storage.end() )
			{
				return &mapFind->second;
			}

			// Getting from parent if possible
			if( _parent )
			{
				return _parent->tagSet( entityTag );
			}

			return nullptr;
		}
    };

	typedef std::unordered_map< Guid, Entity > EntityMap;
	typedef std::unordered_map< Guid, Property > PropertyMap;

	class Blackboard : public IDataEntityManager
	{
		class World* _world{ nullptr };
		EntityMap _entities;
		PropertyMap _properties;
		EntityTagRegister _entityTagRegister;
		const Blackboard* _parent{ nullptr };

	public:
		Blackboard( class World* world, const Blackboard* parent = nullptr )
			: _world( world )
			, _parent( parent )
			, _entityTagRegister( world, parent ? &parent->entityTagRegister() : nullptr ) {};
		Blackboard( const Blackboard& other ) = default;
		~Blackboard() = default;

		World* world() { return _world; }
		const World* world() const { return _world; }

		EntityMap& entities() { return _entities; }
		const EntityMap& entities() const { return _entities; }

		PropertyMap& properties() { return _properties; }
		const PropertyMap& properties() const { return _properties; }

		EntityTagRegister& entityTagRegister() { return _entityTagRegister; }
		const EntityTagRegister& entityTagRegister() const { return _entityTagRegister; }

		const Blackboard* parent() const { return _parent; }
		void setParent( const Blackboard* parent ) { _parent = parent; }

		// IDataEntityManager interface
		class Agent* createAgent( std::string_view name )	override;
		Entity* createEntity( std::string_view name = "", Blackboard* context = nullptr ) override;
		Property* property( const Guid guid )				override;
		const Property* property( const Guid guid )	const	override;
		Entity* entity( const Guid guid )					override;
		const Entity* entity( const Guid guid ) const		override;
		class Agent* agent( const Guid guid )				override;
		const class Agent* agent( const Guid guid ) const	override;
		void removeAgent( const Guid guid )					override { removeEntity( guid ); }
		void removeEntity( const Guid guid )				override { _entities.erase( guid ); }
		void removeProperty( const Guid guid )				override { _properties.erase( guid ); }
		void eraseAll()										override { _entities.clear(); }

		Property* createProperty( Guid guid, StringHash hash, Guid owner = NullGuid, Property::Variant defaultValue = false )	override;

		// Return all string hashes of property names, optionally including the ones from parents
		std::set< StringHash > propertyNameHashes( bool accumulative = false ) const
		{
			std::set< StringHash > hashes;

			// Add property hashes from current Blackboard
			for( const auto& [ guid, property ] : _properties )
			{
				hashes.insert( property.hash() );
			}

			if( accumulative )
			{
				// Recursively add property hashes from the parent Blackboard, if it exists
				if( _parent )
				{
					auto parentHashes = _parent->propertyNameHashes();
					hashes.insert( parentHashes.begin(), parentHashes.end() );
				}
			}

			return hashes;
		}  
	};

	inline Entity* Blackboard::createEntity( std::string_view name, Blackboard* context )
	{
		Entity entity{ _world, name, context };
		auto result = _entities.emplace( entity.guid(), std::move( entity ) );
		if( result.second )
		{
			return &result.first->second;
		}
		return nullptr;
	}

	inline Property* Blackboard::createProperty( Guid guid, StringHash hash, Guid owner, Property::Variant defaultValue )
	{
		if( guid == NullGuid )
		{
			guid = randGuid();
		}
		Property property{ guid, owner, hash };
		auto result = _properties.emplace( guid, std::move( property ) );
		if( result.second )
		{
			result.first->second.value = defaultValue;
			return &result.first->second;
		}
		return nullptr;
	}

	inline Property* Blackboard::property( const Guid guid )
	{
		if( guid == NullGuid )
		{
			return nullptr;
		}

		auto itr = _properties.find( guid );
		if( itr != _properties.end() )
		{
			return &( itr->second );
		}

		if( parent() )
		{
			if( const Property* parentPpt = parent()->property( guid ) )
			{
				auto newPpt = createProperty( guid, parentPpt->hash(), parentPpt->ownerGuid(), parentPpt->value );
				return newPpt;
			}
		}

		return nullptr;
	}

	inline const Property* Blackboard::property( const Guid guid ) const
	{
		if( guid == NullGuid )
		{
			return nullptr;
		}

		auto itr = _properties.find( guid );
		if( itr != _properties.end() )
		{
			return &( itr->second );
		}

		if( parent() )
		{
			return parent()->property( guid );
		}

		return nullptr;
	}

	inline Entity* Blackboard::entity( const Guid guid ) 
	{
		auto itr = _entities.find( guid );
		if( itr != _entities.end() )
		{
			return &( itr->second );
		}

		// as return type is mutable, we need to copy 
		// the entity over from parent if it exists
		if( parent() )
		{
			const auto parentEntity = parent()->entity( guid );
			if( parentEntity )
			{
				auto copiedEntity = _entities.emplace( guid, *parentEntity );
				return copiedEntity.second ? &copiedEntity.first->second : nullptr;
			}
		}

		return nullptr;
	}

	inline const Entity* Blackboard::entity( const Guid guid ) const
	{
		auto itr = _entities.find( guid );
		if( itr != _entities.end() )
		{
			return &( itr->second );
		}

		if( parent() )
		{
			return parent()->entity( guid );
		}

		return nullptr;
	}

	class World : public IDataEntityManager
	{
		Blackboard _context{ this };

    public:
		World() = default;
		~World() = default;

		auto& context() { return _context; };
		const auto& context() const { return _context; };
		auto& properties() { return _context.properties(); };
		const auto& properties() const { return _context.properties(); };

		// IDataEntityManager interface
		class Agent* createAgent( std::string_view name = "" )	override { return _context.createAgent( name ); };
		void removeAgent( const Guid guid )						override { _context.removeAgent( guid ); };
		Entity* createEntity( std::string_view name = "", Blackboard* context = nullptr )		override { return _context.createEntity( name, context ); };
		void removeEntity( const Guid guid )					override { _context.removeEntity( guid ); };
		void removeProperty( const Guid guid )					override { _context.removeProperty( guid ); };
		void eraseAll()											override { _context.eraseAll(); };
		Property* property( const Guid guid )					override { return  _context.property( guid ); }
		const Property* property( const Guid guid ) const		override { return  _context.property( guid ); }
		Entity* entity( const Guid guid )						override { return  _context.entity( guid ); }
		const Entity* entity( const Guid guid ) const			override { return  _context.entity( guid ); }
		class Agent* agent( const Guid guid )					override { return _context.agent( guid ); }
		const class Agent* agent( const Guid guid ) const		override { return _context.agent( guid ); }

		Property* createProperty( Guid guid, StringHash hash, Guid owner = NullGuid, Property::Variant defaultValue = false ) override
		{
			return _context.createProperty( guid, hash, owner, defaultValue );
		};

	};

	inline Entity::Entity( World* world, std::string_view name, Blackboard* context ) noexcept
		: _world( world )
		, _guid( randGuid() )
		, _nameHash( stringHasher( name ) )
		, _context( context ? context : ( world ? &world->context() : nullptr ) )
	{
	}

	inline Property* Entity::property( StringHash hash )
	{
		if( !_world )
		{
			return nullptr;
		}

		// getting guid of property
		auto propertyGuid = _propertyGuids.find( hash );

		// property doesn't exist in this data entity
		if( propertyGuid == _propertyGuids.end() )
		{
			return nullptr;
		}

		// getting actual property
		const auto propertyItr = _world->properties().find( propertyGuid->second );
		if( propertyItr == _world->properties().end() )
		{
			return nullptr;
		}

		return &( propertyItr->second );
	}

	inline const Property* Entity::property( StringHash hash ) const
	{
		if( !_world )
		{
			return nullptr;
		}

		// getting guid of property
		auto propertyGuid = _propertyGuids.find( hash );

		// property doesn't exist in this data entity
		if( propertyGuid == _propertyGuids.end() )
		{
			return nullptr;
		}

		// getting actual property
		const auto propertyItr = _world->properties().find( propertyGuid->second );
		if( propertyItr == _world->properties().end() )
		{
			return nullptr;
		}

		return &( propertyItr->second );
	}

	inline Property* Entity::createProperty( StringHash nameHash )
	{
		if( !_world )
		{
			return nullptr;
		}

		// checking if it's already registered
		if( auto existentProperty = property( nameHash ) )
		{
			return existentProperty;
		}
		
		// registering new property
		Guid newRandGuid{ randGuid() };
		auto emplaceResult = _world->properties().emplace( newRandGuid, std::move( Property{ newRandGuid, _guid, nameHash } ) );

		if( !emplaceResult.second )
		{
			return nullptr;
		}

		// mapping property hash to property guid
		auto registeredPropertyGuid = _propertyGuids.emplace( nameHash, emplaceResult.first->first );
		if( registeredPropertyGuid.second )
		{
			return &( emplaceResult.first->second );
		}

		// erasing property in case its guid could not be registered in data entity
		_world->properties().erase( emplaceResult.first->first );

		return nullptr;
	}

	// Represents a NPC in world context.
	class Agent : public Entity
	{
		Blackboard _opinions;

	public:
		Agent() = delete;
		Agent( World* world, std::string_view name = "" )
			: Entity::Entity( world, name ), _opinions( world, &world->context() ) { };
		Agent( const Agent& agent ) = default;
		Agent( Agent&& agent ) = default;
		~Agent() = default;

		Blackboard& opinions() { return _opinions; }
		const Blackboard& opinions() const { return _opinions; }
	};

	// Represents a NPC in simulation context. World context agent is referenced for opinions.
	class SimAgent
	{
		const Agent* _agent{ nullptr };

	public:
		SimAgent() = delete;
		SimAgent( const Agent* agent ) : _agent( agent ) { };

		const Agent* worldContextAgent() const { return _agent; }
		Guid guid() const { return _agent ? _agent->guid() : NullGuid; }

	};

	inline Agent* Blackboard::agent( const Guid guid )
	{
		Entity* dataEntity = entity( guid );
		return static_cast< Agent* >( dataEntity );
	}

	inline const Agent* Blackboard::agent( const Guid guid ) const
	{
		const Entity* dataEntity = entity( guid );
		return static_cast< const Agent* >( dataEntity );
	}

	inline Agent* Blackboard::createAgent( std::string_view name )
	{
		Agent agent{ _world, name };
		auto result = _entities.emplace( agent.guid(), std::move( agent ) );
		if( result.second )
		{
			return static_cast< Agent* >( &result.first->second );
		}
		return nullptr;
	}

	inline bool isTagged( const Entity* entity, Tag tag )
	{
		if( entity )
		{
			const auto& entityTags = entity->tags();
			return entityTags.find( tag ) != entityTags.cend();
		}
		return false;
	}

	class Heuristic
	{
    public:
		Heuristic() = default;
		~Heuristic() = default;

		struct Result
		{
			float value{ InvalidHeuristic };
		};

		virtual Result calculate( const World& world, const Agent& agent ) { return Result{ }; };

	};

#if defined( GIE_DEBUG )
	class DebugMessages
	{
		std::vector< std::string > _messages;

	public:
		DebugMessages() = default;
		DebugMessages( const DebugMessages& ) = default;
		DebugMessages( DebugMessages&& ) = default;
		~DebugMessages() = default;
		void add( std::string_view message )
		{
			_messages.emplace_back( message );
		}
		const std::vector< std::string >* messages() const
		{
			return &_messages;
		}
	};
#else
	class DebugMessages
	{
		public:
		DebugMessages() = default;
		DebugMessages( const DebugMessages& ) = default;
		DebugMessages( DebugMessages&& ) = default;
		~DebugMessages() = default;
		void add( std::string_view ) { }
		const std::vector< std::string >* messages() const { return nullptr; }
	};
#endif

	typedef Property::Variant ArgumentType;
	typedef std::pair< StringHash, ArgumentType > NamedArgument;

	inline Checksum getChecksum( const std::vector< Guid >& guids )
	{
		Checksum type = NullArguments;
		for( const auto& guid : guids )
		{
			type += static_cast< Checksum >( guid );
		}
		return type;
	}

	inline Checksum getChecksum( std::vector< Guid >&& guids )
	{
		return getChecksum( guids );
	}

	inline Checksum getChecksum( const std::unordered_map< StringHash, ArgumentType >& namedArguments )
	{
		Checksum type = NullArguments;
		for( const auto& namedArgument : namedArguments )
		{
			type += static_cast< Checksum >( namedArgument.first );
		}
		return type;
	}

	inline Checksum getChecksum( std::unordered_map< StringHash, ArgumentType >&& namedArguments )
	{
		return getChecksum( namedArguments );
	}

	class NamedArguments
	{
		// it's checksum of all string hashes in arguments.
		Checksum _type{ NullArguments };
		std::unordered_map< StringHash, ArgumentType > _arguments{ };

		void updateType()
		{
			_type = getChecksum( _arguments );
		}

	public:

		size_t type() const { return _type; }

		NamedArguments() = default;
		NamedArguments( const std::unordered_map< StringHash, ArgumentType >& arguments )
			: _arguments( arguments )
		{
			updateType();
		};

		NamedArguments( std::unordered_map< StringHash, ArgumentType >&& arguments )
			: _arguments( std::move( arguments ) )
		{
			updateType();
		};

		NamedArguments( const NamedArguments& other )
			: _arguments( other._arguments ),
			_type( other._type ) { };

		NamedArguments( NamedArguments&& other )
			: _arguments( std::move( other._arguments ) ),
			_type( other._type ) { };

		~NamedArguments() = default;

		bool empty() const
		{
			return _arguments.empty();
		}

		auto& storage()
		{
			return _arguments;
		}

		const auto& storage() const
		{
			return _arguments;
		}

		void add( const NamedArgument& newArgument )
		{
			auto itr = _arguments.find( newArgument.first );
			if( itr != _arguments.end() )
			{
				itr->second = newArgument.second; // Assign if found
			}
			else
			{
				_arguments.emplace( newArgument ); // Insert if not found
			}
			updateType();
		}

		void add( const std::vector< NamedArgument >& arguments )
		{
			if( arguments.empty() )
			{
				return;
			}

			for( auto& argument : arguments )
			{
				_arguments.emplace( argument );
			}

			updateType();
		}

		void add( std::string_view name, ArgumentType value )
		{
			add( NamedArgument{ stringHasher( name ), value } );
		}

		const ArgumentType* get( std::string_view name ) const
		{
			return get( stringHasher( name.data() ) );
		}

		const ArgumentType* get( const char* name ) const
		{
			return get( stringHasher( name ) );
		}

		const ArgumentType* get( const StringHash hash ) const
		{
			for( const auto& argument : _arguments )
			{
				if( argument.first == hash )
				{
					return &argument.second;
				}
			}
			return nullptr;
		}

		ArgumentType* get( std::string_view name )
		{
			return get( stringHasher( name.data() ) );
		}

		ArgumentType* get( const char* name )
		{
			return get( stringHasher( name ) );
		}

		ArgumentType* get( const StringHash hash )
		{
			for( auto& argument : _arguments )
			{
				if( argument.first == hash )
				{
					return &argument.second;
				}
			}
			return nullptr;
		}
	};

	// Class representing a simulation of a performable action.
	// It stores all information necessary for planner's A* algorithm.
	// It also stores a list of actions to be performed by the agent,
	// in case the simulation is chosed as a valid goal path.
	class Simulation
	{
		friend class Planner;

		Guid _guid{ NullGuid };
		Blackboard _context;
		World* _world{ nullptr };
		SimAgent _simAgent;
		NamedArguments _arguments;
		DebugMessages _debugMessages;

	public:
		Simulation() = delete;
		Simulation( Guid guid, World* world, const Blackboard* parentContext, const SimAgent& simAgent )
			: _world( world ),
			_guid( guid ),
			_context( world, parentContext ),
			_simAgent( simAgent ) { }

		Simulation( Simulation&& ) = default;
		~Simulation() = default;

		Guid guid() const { return _guid; }

		const Blackboard& context() const { return _context; }
		Blackboard& context() { return _context; }

		World* world() { return _world; }
		const World* world() const { return _world; }

		SimAgent& agent() { return _simAgent; }
		const SimAgent& agent() const { return _simAgent; }

		DebugMessages& debugMessages() { return _debugMessages; }
		const DebugMessages& debugMessages() const { return _debugMessages; }

		NamedArguments& arguments() { return _arguments; }
		const NamedArguments& arguments() const { return _arguments; }

		float cost{ MaxCost };
		Heuristic::Result heuristic{ InvalidHeuristic };
		size_t depth{ 0 };

		// comparison operator for std sorting
		bool operator<( const Simulation& other ) const
		{
			return heuristic.value < other.heuristic.value;
		}

		// for std sorting
		static bool smallerThan( const Simulation* lhs, const Simulation* rhs )
		{
			return *lhs < *rhs;
		}

		// Return set of entities with tag
		const std::set< Guid >* tagSet( std::string_view tagName ) const
		{
			return tagSet( stringHasher( tagName ) );
		}

		// Return set of entities with tag
		const std::set< Guid >* tagSet( Tag tag ) const
		{
			auto& simulationTagRegister = context().entityTagRegister();
			const auto simulationTagSet = simulationTagRegister.tagSet( tag );

			// no tag set in simulation context yet, need to check world context
			if( !simulationTagSet )
			{
				return world()->context().entityTagRegister().tagSet( tag );
			}

			return simulationTagSet;
		}

		// Incoming simulation connections.
		std::vector< Guid > incoming;
		// Outgoing simulation connections.
		std::vector< Guid > outgoing;
		// Actions to be performed by agent.
		std::vector< std::shared_ptr< class Action > > actions;
	};

	// Class representing an action to be performed by an agent.
	// It has a state machine for asyncronous execution.
	class Action
	{
		NamedArguments _arguments{ };

    public:
		Action() = default;
		Action( const Action& other ) : _arguments( other._arguments ) { };
		Action( Action&& other ) : _arguments( std::move( other._arguments ) ) { };
		Action( const NamedArguments& arguments ) : _arguments( arguments ) {  };
		Action( NamedArguments&& arguments ) : _arguments( std::move( arguments ) ) {  };
		~Action() = default;

		virtual std::string_view name() const { return stringRegister().get( hash() ); }
		virtual StringHash hash() const { return UndefinedName; }

		NamedArguments& arguments() { return _arguments; }

		enum State : uint8_t
		{
			Waiting,
			Running,
			Paused,
			Aborted,
			Done
		};

		// Cycle of execution of action.
		// @Return Current state of execution.
		virtual State tick( Agent& agent ) { return State::Done; };
	};

	struct EvaluateSimulationParams
	{
	private:
		// Debug messages for simulation.
		DebugMessages* debugMessages;
		// Debug messages for simulation.
		NamedArguments& namedArguments;

	public:
		// Agent which is performing action.
		const SimAgent& agent;
		// Simulation context.
		const Simulation& simulation;
		// Goal which is being achieved.
		const class Goal& goal;

		EvaluateSimulationParams(
			Simulation& simulation,
			const SimAgent& agent,
			const Goal& goal )
			: simulation( simulation )
			, agent( agent )
			, goal( goal )
			, debugMessages( &simulation.debugMessages() )
			, namedArguments( simulation.arguments() )
		{
		}

		void addDebugMessage( std::string_view message )
		{
			if( debugMessages )
			{
				debugMessages->add( message );
			}
		}

		NamedArguments& arguments()
		{
			return namedArguments;
		}
	};

	struct SimulateSimulationParams
	{

	public:
		// Agent which is performing action.
		SimAgent& agent;
		// Simulation context.
		Simulation& simulation;
		// Goal which is being achieved.
		const Goal& goal;
		SimulateSimulationParams(
			Simulation& simulation,
			SimAgent& agent,
			const Goal& goal )
			: simulation( simulation )
			, agent( agent )
			, goal( goal )
		{
		}

		void addDebugMessage( std::string_view message )
		{
			simulation.debugMessages().add( message );
		}

		NamedArguments& arguments()
		{
			return simulation.arguments();
		}
	};

	struct CalculateHeuristicParams : public SimulateSimulationParams {};

	class ActionSimulator
	{
		NamedArguments _arguments{ };

	public:
		ActionSimulator() = delete;
		ActionSimulator( const ActionSimulator& other ) = default;
		ActionSimulator( ActionSimulator&& other ) = default;

		ActionSimulator( const NamedArguments& arguments )
			: _arguments( arguments ) {  };

		ActionSimulator( NamedArguments&& arguments )
			: _arguments( std::move( arguments ) ) {  };

		~ActionSimulator() = default;

		std::string_view name() const { return stringRegister().get( hash() ); }
		virtual StringHash hash() const { return UndefinedName; }

		NamedArguments& arguments() { return _arguments; }
		const NamedArguments& arguments() const { return _arguments; }

		// @Return True in case context meets prerequisites, False otherwise.
		virtual bool evaluate( EvaluateSimulationParams params ) const { return false; }

		// @Return True if simulation setup was done successfuly, False otherwise.
		virtual bool simulate( SimulateSimulationParams params ) const { return false; }

		// Calculates heuristc value for simulation.
		virtual void calculateHeuristic( CalculateHeuristicParams params ) const {}

	};

	// Class representing a specific goal to be achieved by an agent.
	class Goal
	{
	protected:
		World* _world{ nullptr };
		std::vector< Heuristic > _heuristics;

	public:
		Goal() = delete;
		Goal( World& world ) : _world( &world ) { }
		~Goal() = default;

		std::vector< Definition > targets;

		// @Return True if given simulation has reached goal targets, False otherwise.
		bool reached( const Simulation& simulation ) const
		{
			for( auto targetItr = targets.cbegin(); targetItr != targets.cend(); targetItr++ )
			{
				auto ppt = simulation.context().property( targetItr->first );
				if( ppt && ppt->value != targetItr->second )
				{
					return false;
				}
			}
			return true;
		}

		World* world() const { return _world; }
		const auto& heuristis() const { return _heuristics; }

	};

	class ActionSetEntry
	{
	public:
		ActionSetEntry() = default;
		~ActionSetEntry() = default;

		virtual std::string_view name() const { return "None"; }
		virtual StringHash hash() const { return InvalidStringHash; }
		virtual std::shared_ptr< ActionSimulator > simulator( const NamedArguments& arguments ) const { return nullptr; };
		virtual std::shared_ptr< Action > action( const NamedArguments& arguments ) const { return nullptr; };
	};

	// Macro for action class generation.
#define DEFINE_DUMMY_ACTION_CLASS( ActionName ) \
	class ActionName##Action : public gie::Action \
	{ \
		using gie::Action::Action; \
		gie::StringHash hash() const override{ return gie::stringHasher( #ActionName ); } \
	};

	// Macro for action set entry class generation.
	// NOTE: action and simulator classes must be
	// previusly declared and have same action name.
#define DEFINE_ACTION_SET_ENTRY( ActionName ) \
	class ActionName##ActionSetEntry : public gie::ActionSetEntry \
	{ \
		using gie::ActionSetEntry::ActionSetEntry; \
		std::string_view name() const override { return #ActionName; } \
		gie::StringHash hash() const override { return gie::stringHasher( #ActionName ); } \
		std::shared_ptr< gie::ActionSimulator > simulator( const gie::NamedArguments& arguments ) const override \
		{ \
			return std::make_shared< ActionName##Simulator >( arguments ); \
		} \
		std::shared_ptr< gie::Action > action( const gie::NamedArguments& arguments ) const override \
		{ \
			return std::make_shared< ActionName##Action >( arguments ); \
		} \
	};

	// GOAP's planner in charge of holding all simulations and 
	// figuring out most effective action path towards goal.
	class Planner
	{
		using ActionSetType = std::vector< std::shared_ptr< ActionSetEntry > >;

		ActionSetType _actionSet;
		std::unordered_map< Guid, Simulation > _simulations;
		std::vector< std::shared_ptr< Action > > _planActions;
		Simulation* _rootSimulation{ nullptr };
		Goal* _goal{ nullptr };
		Agent* _agent{ nullptr };
		bool _useHeuristics{ false };
		std::string _logContent{ "" };
		size_t _depthLimit{ 10 };

		// simulation which reached goal
		Guid _goalSimulationGuid{ NullGuid };

		// runs simulations towards goal
		void simulate();

		void expandNode( Simulation* openedNode, std::vector< Simulation* >& expandedNodes );

		// backtracks simulations building up plan actions
		void backtrack();

    public:
		Planner() = default;
		Planner( Goal& goal, Agent& agent ) : _goal( &goal ), _agent( &agent ) { }
		~Planner() = default;

		World* world() const { return _goal->world(); }
		Goal* goal() const { return _goal; }
		Agent* agent() const { return _agent; }
		auto& actionSet() { return _actionSet; }
		bool isReady() const { return planStage == PlanStage::Idle || planStage == PlanStage::Done; }
		const Simulation* rootSimulation() const { return _rootSimulation; }
		const std::string& logContent() const { return _logContent; }

		void simulate( Goal& goal, Agent& agent )
		{
			_goal = &goal;
			_agent = &agent;
		}

		template< typename T >
		std::shared_ptr< ActionSetEntry > addActionSetEntry( StringHash hash )
		{
			static_assert( std::is_base_of_v< ActionSetEntry, T >, "Need to be sub class of ActionSetEntry" );
			auto& entry = _actionSet.emplace_back( std::make_shared< T >() );
			return entry;
		}

		std::pair< Guid, Simulation* > createRootSimulation( const Agent* agent )
		{
			_rootSimulation = nullptr;
			Guid newRandGuid{ randGuid() };
			Simulation* newSimulation{ nullptr };
			Simulation sim( newRandGuid, world(), &world()->context(), SimAgent( agent ) );
			auto empl = _simulations.emplace( newRandGuid, std::move( sim ) );
			if( empl.second )
			{
				newSimulation = &empl.first->second;
				_rootSimulation = newSimulation;
				return { newRandGuid, newSimulation };
			}

			return { NullGuid, nullptr };
		}

		std::pair< Guid, Simulation* > createSimulation( Guid currentSimulationGuid = NullGuid )
		{
			auto currentSimulation = simulation( currentSimulationGuid );
			if( currentSimulationGuid != NullGuid && !currentSimulation )
			{
				return { NullGuid, nullptr };
			}

			Guid newRandGuid{ randGuid() };

			if( currentSimulation )
			{
				Simulation sim( newRandGuid, world(), currentSimulation->context().parent(), currentSimulation->agent() );
				auto empl = _simulations.emplace( newRandGuid, std::move( sim ) );
				if( empl.second )
				{
					auto newSimulation = &empl.first->second;
					newSimulation->incoming.emplace_back( currentSimulationGuid );
					newSimulation->depth = currentSimulation->depth + 1;
					currentSimulation->outgoing.emplace_back( newRandGuid );
					return { newRandGuid, newSimulation };
				}
			}
			
			return { NullGuid, nullptr };
		}

		void deleteSimulation( Guid simulationGuid )
		{
			_simulations.erase( simulationGuid );
		}

		const auto& planActions() const { return _planActions; }
		
		Simulation* simulation( Guid guid )
		{
			if( guid == NullGuid )
			{
				return nullptr;
			}
			auto itr = _simulations.find( guid );
			return ( itr != _simulations.end() ? &( itr->second ) : nullptr );
		}

		const Simulation* simulation( Guid guid ) const
		{
			const auto itr = _simulations.find( guid );
			return ( itr != _simulations.cend() ? &( itr->second ) : nullptr );
		}

		void clearSimulations() { _simulations.clear(); }

		const auto& simulations() const { return _simulations; }

		void plan( bool useHeuristic = false );

		enum class PlanStage : uint8_t
		{
			Idle,
			Simulating,
			Backtracking,
			Done
		};

		enum class SimulateStage : uint8_t
		{
			Idle,
			Initializing,
			ExpandingNode,
			Done
		};

		enum class ExpandNodeStage : uint8_t
		{
			Idle,
			Initializing,
			Expanding,
			Done
		};

	private:
		PlanStage planStage{ PlanStage::Idle };
		SimulateStage simulateStage{ SimulateStage::Idle };
		ExpandNodeStage expandNodeStage{ ExpandNodeStage::Idle };
		bool step{ true };
		bool logSteps{ true };
		std::vector< Simulation* >::iterator openedNodesItr{};
		std::vector< Simulation* > openedNodes{};
		std::vector< Simulation* > newNodes{};
		Simulation* baseSimulation{ nullptr };
		ActionSetType::iterator actionSetItr{};
		std::shared_ptr< ActionSetEntry > actionSetEntry{ nullptr };
		std::shared_ptr< ActionSimulator > actionSimulator{ nullptr };
		std::pair< Guid, Simulation* > newSimulationPair{ NullGuid, nullptr };

	public:
		bool& logStepsMutator() { return logSteps; }
		bool& stepMutator() { return step; }
	};

#define SET_PLAN_STAGE( from, to ) \
	if( planStage == Planner::PlanStage::from ) \
	{ \
		planStage = Planner::PlanStage::to; \
	}

#define SET_SIMULATE_STAGE( from, to ) \
	if( simulateStage == Planner::SimulateStage::from ) \
	{ \
		simulateStage = Planner::SimulateStage::to; \
	}

#define SET_EXPANDNODE_STAGE( from, to ) \
	if( expandNodeStage == Planner::ExpandNodeStage::from ) \
	{ \
		expandNodeStage = Planner::ExpandNodeStage::to; \
	}

	inline void Planner::plan( bool useHeuristic )
	{
		SET_PLAN_STAGE( Done, Idle );

		if( planStage == PlanStage::Idle )
		{
			_useHeuristics = useHeuristic;
			planStage = PlanStage::Simulating;
		}

		if( planStage == PlanStage::Simulating )
		{
			simulate();

			if( simulateStage == SimulateStage::Done )
			{
				planStage = PlanStage::Backtracking;
			}
		}

		if( planStage == PlanStage::Backtracking )
		{
			backtrack();
			planStage = PlanStage::Done;
		}
	}

	inline void Planner::simulate()
	{
		SET_SIMULATE_STAGE( Done, Idle );
		SET_SIMULATE_STAGE( Idle, Initializing );

		if( step && simulateStage == SimulateStage::ExpandingNode )
		{
			goto expandNodeLoop;
		}

		// validating world, agent and goal
		if( !agent() || !world() || !goal() )
		{
			return;
		}

		// resetting simulation
		_goalSimulationGuid = NullGuid;
		_simulations.clear();
		_rootSimulation = nullptr;

		// logging
		_logContent = "";
		if( logSteps ) _logContent.append( "Planner::simulate() started!\n" );
		if( logSteps ) _logContent.append( "Creating root simulation\n" );

		// creating root simulation node
		createRootSimulation( agent() );

		if( !_rootSimulation )
		{
			return;
		}

		// starting search by expanding root node
		openedNodes = { _rootSimulation };

		if( logSteps ) _logContent.append( "Starting !openedNodes.empty() while loop.\n" );

		SET_SIMULATE_STAGE( Initializing, ExpandingNode );

		while( !openedNodes.empty() )
		{
			// simulation nodes created during this loop, to be added to opened nodes
			newNodes.clear();

			if( _useHeuristics )
			{
				if( logSteps ) _logContent.append( "Expanding high priority node.\n" );

				auto highPriorityOpenedNode = *openedNodes.end();
				openedNodes.pop_back();
				expandNode( highPriorityOpenedNode, newNodes );

				if( logSteps ) _logContent.append( "Adding " ).append( std::to_string( newNodes.size() ) ).append( " new nodes (sorted).\n" );

				for( auto newNode : newNodes )
				{
					auto insertPos = std::upper_bound( openedNodes.begin(), openedNodes.end(), newNode, &Simulation::smallerThan );
					openedNodes.insert( insertPos, newNode );
				}
			}
			else
			{
				if( logSteps ) _logContent.append( "Expanding all opened nodes.\n" );

				openedNodesItr = openedNodes.begin();
expandNodeLoop:
				// go through all opened nodes
				while( openedNodesItr != openedNodes.end() )
				{
					expandNode( *openedNodesItr, newNodes );

					if( step && expandNodeStage != ExpandNodeStage::Done )
					{
						return;
					}

					// checking if last node satified goal
					if( !newNodes.empty() && goal()->reached( *newNodes.back() ) )
					{
						if( logSteps ) _logContent.append( "Goal reached, stopping simulations!\n" );
						// marking simulation which reached the goal for backtracking
						_goalSimulationGuid = ( *newNodes.end() )->guid();
						// no need to keep expanding nodes
						newNodes.clear();
						openedNodes.clear();
						break;
					}
					openedNodesItr++;
					if( step ) return;
				}

				// setting next iteration
				openedNodes = newNodes;
			}
		}

		if( logSteps ) _logContent.append( "Simulation finished, no more opened nodes.\n" );

		SET_SIMULATE_STAGE( ExpandingNode, Done );
	}

	inline void Planner::expandNode( Simulation* openedNode, std::vector< Simulation* >& newNodes )
	{
		SET_EXPANDNODE_STAGE( Done, Idle );
		SET_EXPANDNODE_STAGE( Idle, Initializing );

		if( step && expandNodeStage == ExpandNodeStage::Expanding )
		{
			goto expandNodeLoop;
		}

		if( logSteps ) _logContent.append( "* Getting base simulation.\n" );
		// getting simulation from opened node
		baseSimulation = openedNode;
		if( !baseSimulation )
		{
			if( logSteps ) _logContent.append( "* No base simulation found!\n" );
			expandNodeStage = ExpandNodeStage::Done;
			return;
		}

		// cannot expand this node if it has reached simulation depth limit
		if( baseSimulation->depth >= _depthLimit )
		{
			if( logSteps ) _logContent.append( "* Reached max depth, skipping opened node.\n" );
			expandNodeStage = ExpandNodeStage::Done;
			return;
		}

		if( logSteps ) _logContent.append( "* Expanding opened node with available actions.\n" );

		actionSetItr = _actionSet.begin();

		SET_EXPANDNODE_STAGE( Initializing, Expanding );
		if( step ) return;

expandNodeLoop:

		// expanding simulation over all available actions
		while( actionSetItr != _actionSet.end() )
		{
			actionSetEntry = *actionSetItr;

			if( logSteps ) _logContent.append( "\n* Expanding for Action: " ).append( actionSetEntry->name() ).append( "\n" );

			// getting simulator for action
			actionSimulator = actionSetEntry->simulator( {} );
			if( !actionSimulator )
			{
				if( logSteps ) _logContent.append( "* Error on instantiating action simulator!\n" );
				actionSetItr++;
				if( step ) return;
				continue;
			}

			if( logSteps ) _logContent.append( "* Checking evaluate.\n" );

			// checking if current simulation context meets action's conditions
			if( !actionSimulator->evaluate( { *baseSimulation, baseSimulation->agent(), *goal() } ) )
			{
				if( logSteps ) _logContent.append( "* Failed on evaluate, skipping action.\n" );
				actionSetItr++;
				if( step ) return;
				continue;
			}

			if( logSteps ) _logContent.append( "* Creating simulation node for action.\n" );

			// creating new simulation node for action simulation
			newSimulationPair = createSimulation( openedNode->guid() );

			if( logSteps ) _logContent.append( "* Simulating action.\n" );

			// running action simulation on new node
			bool simulationSuccess = actionSimulator->simulate( { *newSimulationPair.second, newSimulationPair.second->agent(), *goal() } );

			// removing new node in case simulation has failed
			if( !simulationSuccess )
			{
				if( logSteps ) _logContent.append( "* Simulation failed, deleting current simulation node.\n" );
				deleteSimulation( newSimulationPair.first );
				actionSetItr++;
				if( step ) return;
				continue;
			}

			if( logSteps ) _logContent.append( "* Simulation successful.\n" );

			// cannot expand this node if it has reached simulation depth limit
			if( newSimulationPair.second->depth >= _depthLimit )
			{
				if( logSteps ) _logContent.append( "* Reached max depth, not further expanding current node.\n" );
			}
			else
			{
				if( logSteps ) _logContent.append( "* Marking node for further expansion.\n" );
				// keeping track of expanded nodes
				newNodes.push_back( newSimulationPair.second );
			}

			// stopping simulation loop if goal has been reached here
			if( goal()->reached( *newSimulationPair.second ) )
			{
				if( logSteps ) _logContent.append( "* Goal reached, stopped expanding node!\n" );
				expandNodeStage = ExpandNodeStage::Done;
				break;
			}

			actionSetItr++;
		}

		SET_EXPANDNODE_STAGE( Expanding, Done );
	}

	inline void Planner::backtrack()
	{
		// stop if no goal simulation node is assigned
		if( _goalSimulationGuid == NullGuid )
		{
			return;
		}

		// checking if simulation node exists
		auto goalSimulationNode = _simulations.find( _goalSimulationGuid );
		if( goalSimulationNode == _simulations.end() )
		{
			return;
		}

		// cleaning previous planned actions
		_planActions.clear();

		// backtracking from leaf to root simulation nodes
		Guid currentSimulationNode = _goalSimulationGuid;
		while( currentSimulationNode != NullGuid )
		{
			auto simulationNode = _simulations.find( currentSimulationNode );
			if( simulationNode == _simulations.end() )
			{
				break;
			}

			_planActions.insert( _planActions.end(), simulationNode->second.actions.begin(), simulationNode->second.actions.end() );
			
			if( simulationNode->second.incoming.empty() )
			{
				currentSimulationNode = NullGuid;
				break;
			}

			currentSimulationNode = simulationNode->second.incoming.front();
		}

		
	}

}

#endif