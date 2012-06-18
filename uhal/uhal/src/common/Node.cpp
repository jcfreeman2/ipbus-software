#include "uhal/Node.hpp"

#include "uhal/HwInterface.hpp"
#include "uhal/ValMem.hpp"
#include "uhal/NodeTreeBuilder.hpp"
#include "uhal/Utilities.hpp"

#include "log/log.hpp"

#include <iomanip>


std::ostream& operator<< ( std::ostream& aStream , const uhal::Node& aNode )
{
	using namespace uhal;

	try
	{
		aNode.stream ( aStream );
		return aStream;
	}
	catch ( const std::exception& aExc )
	{
		log ( Error() , "Exception \"" , aExc.what() , "\" caught at " , ThisLocation() );
		throw uhal::exception ( aExc );
	}
}


namespace uhal
{
	template < >
	void log_inserter< uhal::Node > ( const uhal::Node& aNode )
	{
		std::stringstream lStream;
		aNode.stream ( lStream );
		std::istreambuf_iterator<char> lEnd;
		std::istreambuf_iterator<char> lIt ( lStream.rdbuf() );

		while ( lIt!=lEnd )
		{
			fputc ( *lIt++ , log_configuration::getDestination() );
		}
	}
}




namespace uhal
{
	boost::shared_ptr< uhal::Node > clone ( const boost::shared_ptr< uhal::Node >& aNode )
	{
		return boost::shared_ptr< uhal::Node > ( new Node ( *aNode ) );
	}

	boost::shared_ptr< uhal::Node > clone ( const boost::shared_ptr< const uhal::Node >& aNode )
	{
		return boost::shared_ptr< uhal::Node > ( new Node ( *aNode ) );
	}
}


namespace uhal
{

	Node::permissions_lut::permissions_lut()
	{
		add
		( "r"			, defs::READ )
		( "w"			, defs::WRITE )
		( "read"		, defs::READ )
		( "write"		, defs::WRITE )
		( "rw"			, defs::READWRITE )
		( "wr"			, defs::READWRITE )
		( "readwrite"	, defs::READWRITE )
		( "writeread"	, defs::READWRITE )
		;
	}

	const Node::permissions_lut Node::mPermissionsLut;


	Node::mode_lut::mode_lut()
	{
		add
		( "single"			, defs::SINGLE )
		( "block"			, defs::INCREMENTAL )
		( "port"			, defs::NON_INCREMENTAL )
		( "incremental"		, defs::INCREMENTAL )
		( "non-incremental"	, defs::NON_INCREMENTAL )
		( "inc"				, defs::INCREMENTAL )
		( "non-inc"			, defs::NON_INCREMENTAL )
		;
	}

	const Node::mode_lut Node::mModeLut;








Node::Node ( const pugi::xml_node& aXmlNode , const uint32_t& aParentAddr , const uint32_t& aParentMask ) try :
		mHw ( NULL ),
			mUid ( "" ),
			mAddr ( 0x00000000 ),
			mAddrMask ( 0x00000000 ),
			mMask ( defs::NOMASK ),
			mPermission ( defs::READWRITE ),
			mMode ( defs::SINGLE ),
			mChildrenMap ( )
	{
		if ( ! uhal::utilities::GetXMLattribute<true> ( aXmlNode , "id" , mUid ) )
		{
			//error description is given in the function itself so no more elaboration required
			log ( Error() , "Throwing at " , ThisLocation() );
			throw NodeMustHaveUID();
		}

		uint32_t lAddr ( 0x00000000 );

		if ( uhal::utilities::GetXMLattribute<false> ( aXmlNode , "address" , lAddr ) )
		{
			if ( lAddr & ~aParentMask )
			{
				log ( Error() , "Node address " , Integer ( lAddr, IntFmt< hex , fixed >() ) ,
					  " overlaps with the mask specified by the parent node, " , Integer ( aParentMask, IntFmt< hex , fixed >() ) );
				log ( Error() , "Throwing at " , ThisLocation() );
				throw ChildHasAddressOverlap();
			}

			mAddr = aParentAddr | ( lAddr & aParentMask );
		}
		else
		{
			mAddr = aParentAddr;
		}

		if ( uhal::utilities::GetXMLattribute<false> ( aXmlNode , "address-mask" , mAddrMask ) )
		{
			if ( mAddrMask & ~aParentMask )
			{
				log ( Error() , "Node address mask " , Integer ( mAddrMask, IntFmt< hex , fixed >() ) ,
					  " overlaps with the parent mask " , Integer ( aParentMask, IntFmt< hex , fixed >() ) ,
					  ". This makes the child's address subspace larger than the parent and is not allowed" );
				log ( Error() , "Throwing at " , ThisLocation() );
				throw ChildHasAddressMaskOverlap();
			}
		}
		else
		{
			//we have already checked that the address doesn't overlap with the parent mask, therefore, this calculation cannot produce a mask which overlaps the parent mask either.
			mAddrMask = 0x00000001 ;

			for ( uint32_t i=0 ; i!=32 ; ++i )
			{
				if ( mAddrMask & mAddr )
				{
					break;
				}

				mAddrMask = ( mAddrMask << 1 ) | 0x00000001;
			}

			mAddrMask &= ~mAddr;
		}

		std::string lModule;

		if ( uhal::utilities::GetXMLattribute<false> ( aXmlNode , "module" , lModule ) )
		{
			try
			{
				boost::shared_ptr< Node > lNode ( clone ( NodeTreeBuilder::getInstance().getNodeTree ( lModule , mAddr , mAddrMask ) ) );
				mChildrenMap.insert ( std::make_pair ( lNode->mUid , lNode ) );

				for ( std::hash_map< std::string , boost::shared_ptr<Node> >::iterator lSubMapIt = lNode->mChildrenMap.begin() ; lSubMapIt != lNode->mChildrenMap.end() ; ++lSubMapIt )
				{
					mChildrenMap.insert ( std::make_pair ( ( lNode->mUid ) +'.'+ ( lSubMapIt->first ) , lSubMapIt->second ) ); //no clone needed here because we want the shared_ptr to point to the same object
				}
			}
			catch ( const std::exception& aExc )
			{
				log ( Error() , "Exception \"" , aExc.what() , "\" caught at " , ThisLocation() );
				throw uhal::exception ( aExc );
			}
		}
		else
		{
			uhal::utilities::GetXMLattribute<false> ( aXmlNode , "mask" , mMask );
			std::string lPermission;

			if ( uhal::utilities::GetXMLattribute<false> ( aXmlNode , "permission" , lPermission ) )
			{
				try
				{
					boost::spirit::qi::phrase_parse (
						lPermission.begin(),
						lPermission.end(),
						Node::mPermissionsLut,
						boost::spirit::ascii::space,
						mPermission
					);
				}
				catch ( const std::exception& aExc )
				{
					log ( Error() , "Exception \"" , aExc.what() , "\" caught at " , ThisLocation() );
					throw uhal::exception ( aExc );
				}
			}

			std::string lMode;

			if ( uhal::utilities::GetXMLattribute<false> ( aXmlNode , "mode" , lMode ) )
			{
				try
				{
					boost::spirit::qi::phrase_parse (
						lMode.begin(),
						lMode.end(),
						Node::mModeLut,
						boost::spirit::ascii::space,
						mMode
					);
				}
				catch ( const std::exception& aExc )
				{
					log ( Error() , "Exception \"" , aExc.what() , "\" caught at " , ThisLocation() );
					throw uhal::exception ( aExc );
				}
			}

			for ( pugi::xml_node lXmlNode = aXmlNode.child ( "node" ); lXmlNode; lXmlNode = lXmlNode.next_sibling ( "node" ) )
			{
				boost::shared_ptr< Node > lNode ( clone ( NodeTreeBuilder::getInstance().create ( lXmlNode , mAddr , mAddrMask ) ) );
				mChildrenMap.insert ( std::make_pair ( lNode->mUid , lNode ) );

				for ( std::hash_map< std::string , boost::shared_ptr<Node> >::iterator lSubMapIt = lNode->mChildrenMap.begin() ; lSubMapIt != lNode->mChildrenMap.end() ; ++lSubMapIt )
				{
					mChildrenMap.insert ( std::make_pair ( ( lNode->mUid ) +'.'+ ( lSubMapIt->first ) , lSubMapIt->second ) ); //no clone needed here because we want the shared_ptr to point to the same object
				}
			}
		}

		// log( Info() , "Node \"" , mUid , "\" has the following mChildrenMap entries:" );
		// for ( std::hash_map< std::string , boost::shared_ptr<Node> >::iterator lIt = mChildrenMap.begin() ; lIt != mChildrenMap.end() ; ++lIt )
		// {
		// log( Info() , "\t> " , lIt->first );
		// }
	}
	catch ( const std::exception& aExc )
	{
		log ( Error() , "Exception \"" , aExc.what() , "\" caught at " , ThisLocation() );
		throw uhal::exception ( aExc );
	}


Node::Node ( const Node& aNode ) try :
		mHw ( aNode.mHw ),
			mUid ( aNode.mUid ),
			mAddr ( aNode.mAddr ),
			mAddrMask ( aNode.mAddrMask ),
			mMask ( aNode.mMask ),
			mPermission ( aNode.mPermission ),
			mMode ( aNode.mMode ),
			mChildrenMap ()
	{
		for ( std::hash_map< std::string , boost::shared_ptr<Node> >::const_iterator lIt = aNode.mChildrenMap.begin(); lIt != aNode.mChildrenMap.end(); ++lIt )
		{
			mChildrenMap.insert ( std::make_pair ( lIt->first , clone ( lIt->second ) ) );
		}
	}
	catch ( const std::exception& aExc )
	{
		log ( Error() , "Exception \"" , aExc.what() , "\" caught at " , ThisLocation() );
		throw uhal::exception ( aExc );
	}


	Node& Node::operator= ( const Node& aNode )
	{
		try
		{
			mHw = aNode.mHw;
			mUid = aNode.mUid;
			mAddr = aNode.mAddr;
			mAddrMask = aNode.mAddrMask;
			mMask = aNode.mMask;
			mPermission = aNode.mPermission;
			mMode = aNode.mMode;

			for ( std::hash_map< std::string , boost::shared_ptr<Node> >::const_iterator lIt = aNode.mChildrenMap.begin(); lIt != aNode.mChildrenMap.end(); ++lIt )
			{
				mChildrenMap.insert ( std::make_pair ( lIt->first , clone ( lIt->second ) ) );
			}

			return *this;
		}
		catch ( const std::exception& aExc )
		{
			log ( Error() , "Exception \"" , aExc.what() , "\" caught at " , ThisLocation() );
			throw uhal::exception ( aExc );
		}
	}


	Node::~Node() {}

	bool Node::operator == ( const Node& aNode )
	{
		try
		{
			return this->getAddress() == aNode.getAddress() &&
				   this->getMask() == aNode.getMask() &&
				   this->getPermission() == aNode.getPermission() &&
				   this->getId() == aNode.getId();
		}
		catch ( const std::exception& aExc )
		{
			log ( Error() , "Exception \"" , aExc.what() , "\" caught at " , ThisLocation() );
			throw uhal::exception ( aExc );
		}
	}


	std::string Node::getId() const
	{
		try
		{
			return mUid;
		}
		catch ( const std::exception& aExc )
		{
			log ( Error() , "Exception \"" , aExc.what() , "\" caught at " , ThisLocation() );
			throw uhal::exception ( aExc );
		}
	}

	uint32_t Node::getAddress() const
	{
		try
		{
			return mAddr;
		}
		catch ( const std::exception& aExc )
		{
			log ( Error() , "Exception \"" , aExc.what() , "\" caught at " , ThisLocation() );
			throw uhal::exception ( aExc );
		}
	}

	uint32_t Node::getMask() const
	{
		try
		{
			return mMask;
		}
		catch ( const std::exception& aExc )
		{
			log ( Error() , "Exception \"" , aExc.what() , "\" caught at " , ThisLocation() );
			throw uhal::exception ( aExc );
		}
	}

	defs::NodePermission Node::getPermission() const
	{
		try
		{
			return mPermission;
		}
		catch ( const std::exception& aExc )
		{
			log ( Error() , "Exception \"" , aExc.what() , "\" caught at " , ThisLocation() );
			throw uhal::exception ( aExc );
		}
	}


	void Node::stream ( std::ostream& aStream , std::size_t aIndent ) const
	{
		try
		{
			aStream << std::hex << std::setfill ( '0' ) << std::uppercase;
			aStream << '\n' << std::string ( aIndent , ' ' ) << "+ ";
			aStream << "Node \"" << mUid << "\", ";
			aStream << "Address 0x" << std::setw ( 8 ) << mAddr << ", ";
			aStream << "Address Mask 0x" << std::setw ( 8 ) << mAddrMask << ", ";
			aStream << "Mask 0x" << std::setw ( 8 ) << mMask << ", ";
			aStream << "Permissions " << ( mPermission&defs::READ?'r':'-' ) << ( mPermission&defs::WRITE?'w':'-' ) << ", ";

			switch ( mMode )
			{
				case defs::SINGLE:
					aStream << "Mode SINGLE register access";
					break;
				case defs::INCREMENTAL:
					aStream << "Mode INCREMENTAL block access";
					break;
				case defs::NON_INCREMENTAL:
					aStream << "Mode NON-INCREMENTAL block access";
					break;
			}

			for ( std::hash_map< std::string , boost::shared_ptr<Node> >::const_iterator lIt = mChildrenMap.begin(); lIt != mChildrenMap.end(); ++lIt )
			{
				lIt->second->stream ( aStream , aIndent+2 );
			}
		}
		catch ( const std::exception& aExc )
		{
			log ( Error() , "Exception \"" , aExc.what() , "\" caught at " , ThisLocation() );
			throw uhal::exception ( aExc );
		}
	}


	Node& Node::getNode ( const std::string& aId )
	{
		try
		{
			std::hash_map< std::string , boost::shared_ptr<Node> >::iterator lIt = mChildrenMap.find ( aId );

			if ( lIt==mChildrenMap.end() )
			{
				log ( Error() , "No branch found with ID-path \"" , aId , "\"" );
				std::size_t lPos ( std::string::npos );
				bool lPartialMatch ( false );

				while ( true )
				{
					lPos = aId.rfind ( '.' , lPos );

					if ( lPos == std::string::npos )
					{
						break;
					}

					std::hash_map< std::string , boost::shared_ptr<Node> >::iterator lIt = mChildrenMap.find ( aId.substr ( 0 , lPos ) );

					if ( lIt!=mChildrenMap.end() )
					{
						log ( Error() , "Partial match \"" , aId.substr ( 0 , lPos ) , "\" found for ID-path \"" , aId , "\"" );
						log ( Error() , "Tree structure of partial match is:" , * ( lIt->second ) );
						lPartialMatch = true;
						break;
					}

					lPos--;
				}

				if ( !lPartialMatch )
				{
					log ( Error() , "Not even a partial match found for ID-path \"" , aId , "\". Tree structure is:" , *this );
				}

				log ( Error() , "Throwing at " , ThisLocation() );
				throw NoBranchFoundWithGivenUID();
			}

			return * ( lIt->second );
		}
		catch ( const std::exception& aExc )
		{
			log ( Error() , "Exception \"" , aExc.what() , "\" caught at " , ThisLocation() );
			throw uhal::exception ( aExc );
		}
	}


	std::vector<std::string> Node::getNodes()
	{
		try
		{
			std::vector<std::string> lNodes;
			lNodes.reserve ( mChildrenMap.size() ); //prevent reallocations

			for ( std::hash_map< std::string , boost::shared_ptr<Node> >::iterator lIt = mChildrenMap.begin(); lIt != mChildrenMap.end(); ++lIt )
			{
				lNodes.push_back ( lIt->second->mUid );
			}

			return lNodes;
		}
		catch ( const std::exception& aExc )
		{
			log ( Error() , "Exception \"" , aExc.what() , "\" caught at " , ThisLocation() );
			throw uhal::exception ( aExc );
		}
	}

	//Given a regex return the ids that match the
	std::vector<std::string> Node::getNodes ( const boost::regex& aRegex )
	{
		try
		{
			std::vector<std::string> lNodes;
			lNodes.reserve ( mChildrenMap.size() ); //prevent reallocations
			log ( Info() , "Regular Expression : " , aRegex.str() );

			for ( std::hash_map< std::string , boost::shared_ptr<Node> >::iterator lIt = mChildrenMap.begin(); lIt != mChildrenMap.end(); ++lIt )
			{
				boost::cmatch lMatch;

				if ( boost::regex_match ( lIt->first.c_str() , lMatch , aRegex ) ) //to allow partial match, add  boost::match_default|boost::match_partial  as fourth argument
				{
					log ( Info() , lIt->first , " matches" );
					lNodes.push_back ( lIt->first );
				}
			}

			//bit dirty but since the hash map sorts them by the hash, not the value, they are completely scrambled here making it very hard to use.
			std::sort ( lNodes.begin(), lNodes.end() );
			return lNodes;
		}
		catch ( const std::exception& aExc )
		{
			log ( Error() , "Exception \"" , aExc.what() , "\" caught at " , ThisLocation() );
			throw uhal::exception ( aExc );
		}
	}

	std::vector<std::string> Node::getNodes ( const char* aRegex )
	{
		try
		{
			return getNodes ( boost::regex ( aRegex ) );
		}
		catch ( const std::exception& aExc )
		{
			log ( Error() , "Exception \"" , aExc.what() , "\" caught at " , ThisLocation() );
			throw uhal::exception ( aExc );
		}
	}

	std::vector<std::string> Node::getNodes ( const std::string& aRegex )
	{
		try
		{
			return getNodes ( boost::regex ( aRegex ) );
		}
		catch ( const std::exception& aExc )
		{
			log ( Error() , "Exception \"" , aExc.what() , "\" caught at " , ThisLocation() );
			throw uhal::exception ( aExc );
		}
	}

	void Node::write ( const uint32_t& aValue )
	{
		try
		{
			if ( mPermission & defs::WRITE )
			{
				if ( mMask == defs::NOMASK )
				{
					mHw->getClient()->write ( mAddr , aValue );
				}
				else
				{
					mHw->getClient()->write ( mAddr , aValue , mMask );
				}
			}
			else
			{
				log ( Error() , "Node permissions denied write access" );
				log ( Error() , "Throwing at " , ThisLocation() );
				throw WriteAccessDenied();
			}
		}
		catch ( const std::exception& aExc )
		{
			log ( Error() , "Exception \"" , aExc.what() , "\" caught at " , ThisLocation() );
			throw uhal::exception ( aExc );
		}
	}


	void Node::writeBlock ( const std::vector< uint32_t >& aValues ) // , const defs::BlockReadWriteMode& aMode )
	{
		try
		{
			if ( ( mMode == defs::SINGLE ) && ( aValues.size() != 1 ) ) //We allow the user to call a bulk access of size=1 to a single register
			{
				log ( Error() , "Bulk Transfer requested on single register node" );
				log ( Error() , "If you were expecting an incremental write, please modify your address file to add the 'mode=\"incremental\"' flags there" );
				log ( Error() , "Throwing at " , ThisLocation() );
				throw BulkTransferOnSingleRegister();
			}
			else
			{
				if ( mPermission & defs::WRITE )
				{
					mHw->getClient()->writeBlock ( mAddr , aValues , mMode ); //aMode );
				}
				else
				{
					log ( Error() , "Node permissions denied write access" );
					log ( Error() , "Throwing at " , ThisLocation() );
					throw WriteAccessDenied();
				}
			}
		}
		catch ( const std::exception& aExc )
		{
			log ( Error() , "Exception \"" , aExc.what() , "\" caught at " , ThisLocation() );
			throw uhal::exception ( aExc );
		}
	}


	ValWord< uint32_t > Node::read()
	{
		try
		{
			if ( mPermission & defs::READ )
			{
				if ( mMask == defs::NOMASK )
				{
					return mHw->getClient()->read ( mAddr );
				}
				else
				{
					return mHw->getClient()->read ( mAddr , mMask );
				}
			}
			else
			{
				log ( Error() , "Node permissions denied read access" );
				log ( Error() , "Throwing at " , ThisLocation() );
				throw ReadAccessDenied();
			}
		}
		catch ( const std::exception& aExc )
		{
			log ( Error() , "Exception \"" , aExc.what() , "\" caught at " , ThisLocation() );
			throw uhal::exception ( aExc );
		}
	}


	ValVector< uint32_t > Node::readBlock ( const uint32_t& aSize ) //, const defs::BlockReadWriteMode& aMode )
	{
		try
		{
			if ( ( mMode == defs::SINGLE ) && ( aSize != 1 ) ) //We allow the user to call a bulk access of size=1 to a single register
			{
				log ( Error() , "Bulk Transfer requested on single register node" );
				log ( Error() , "If you were expecting an incremental read, please modify your address file to add the 'mode=\"incremental\"' flags there" );
				log ( Error() , "Throwing at " , ThisLocation() );
				throw BulkTransferOnSingleRegister();
			}
			else
			{
				if ( mPermission & defs::READ )
				{
					return mHw->getClient()->readBlock ( mAddr , aSize , mMode ); //aMode );
				}
				else
				{
					log ( Error() , "Node permissions denied read access" );
					log ( Error() , "Throwing at " , ThisLocation() );
					throw ReadAccessDenied();
				}
			}
		}
		catch ( const std::exception& aExc )
		{
			log ( Error() , "Exception \"" , aExc.what() , "\" caught at " , ThisLocation() );
			throw uhal::exception ( aExc );
		}
	}

	ValWord< int32_t > Node::readSigned()
	{
		try
		{
			if ( mPermission & defs::READ )
			{
				if ( mMask == defs::NOMASK )
				{
					return mHw->getClient()->readSigned ( mAddr );
				}
				else
				{
					return mHw->getClient()->readSigned ( mAddr , mMask );
				}
			}
			else
			{
				log ( Error() , "Node permissions denied read access" );
				log ( Error() , "Throwing at " , ThisLocation() );
				throw ReadAccessDenied();
			}
		}
		catch ( const std::exception& aExc )
		{
			log ( Error() , "Exception \"" , aExc.what() , "\" caught at " , ThisLocation() );
			throw uhal::exception ( aExc );
		}
	}


	ValVector< int32_t > Node::readBlockSigned ( const uint32_t& aSize ) //, const defs::BlockReadWriteMode& aMode )
	{
		try
		{
			if ( ( mMode == defs::SINGLE ) && ( aSize != 1 ) ) //We allow the user to call a bulk access of size=1 to a single register
			{
				log ( Error() , "Bulk Transfer requested on single register node" );
				log ( Error() , "If you were expecting an incremental read, please modify your address file to add the 'mode=\"incremental\"' flags there" );
				log ( Error() , "Throwing at " , ThisLocation() );
				throw BulkTransferOnSingleRegister();
			}
			else
			{
				if ( mPermission & defs::READ )
				{
					return mHw->getClient()->readBlockSigned ( mAddr , aSize , mMode ); //aMode );
				}
				else
				{
					log ( Error() , "Node permissions denied read access" );
					log ( Error() , "Throwing at " , ThisLocation() );
					throw ReadAccessDenied();
				}
			}
		}
		catch ( const std::exception& aExc )
		{
			log ( Error() , "Exception \"" , aExc.what() , "\" caught at " , ThisLocation() );
			throw uhal::exception ( aExc );
		}
	}




	ValWord< uint32_t > Node::rmw_bits ( const uint32_t& aANDterm , const uint32_t& aORterm )
	{
		try
		{
			if ( mPermission == defs::READWRITE )
			{
				return mHw->getClient()->rmw_bits ( mAddr , aANDterm , aORterm );
			}
			else
			{
				log ( Error() , "Node permissions denied read/write access" );
				log ( Error() , "Throwing at " , ThisLocation() );
				throw ReadAccessDenied();
			}
		}
		catch ( const std::exception& aExc )
		{
			log ( Error() , "Exception \"" , aExc.what() , "\" caught at " , ThisLocation() );
			throw uhal::exception ( aExc );
		}
	}



	ValWord< int32_t > Node::rmw_sum ( const int32_t& aAddend )
	{
		try
		{
			if ( mPermission == defs::READWRITE )
			{
				return mHw->getClient()->rmw_sum ( mAddr , aAddend );
			}
			else
			{
				log ( Error() , "Node permissions denied read/write access" );
				log ( Error() , "Throwing at " , ThisLocation() );
				throw ReadAccessDenied();
			}
		}
		catch ( const std::exception& aExc )
		{
			log ( Error() , "Exception \"" , aExc.what() , "\" caught at " , ThisLocation() );
			throw uhal::exception ( aExc );
		}
	}




	boost::shared_ptr<ClientInterface> Node::getClient()
	{
		try
		{
			return mHw->getClient();
		}
		catch ( const std::exception& aExc )
		{
			log ( Error() , "Exception \"" , aExc.what() , "\" caught at " , ThisLocation() );
			throw uhal::exception ( aExc );
		}
	}

}
