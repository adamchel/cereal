#ifndef CEREAL_ARCHIVES_BSON_HPP_
#define CEREAL_ARCHIVES_BSON_HPP_

#include <stack>
#include <limits>
#include <sstream>
#include <stack>
#include <vector>
#include <string>

#include <cereal/cereal.hpp>
#include <cereal/details/util.hpp>

#include <bsoncxx/builder/core.hpp>

namespace cereal
{

  template <class BsonT>
  struct is_bson {
      static constexpr bool value = std::is_same<BsonT, bsoncxx::types::b_double>::value   ||
                                 std::is_same<BsonT, bsoncxx::types::b_utf8>::value     ||
                                 std::is_same<BsonT, bsoncxx::types::b_document>::value ||
                                 std::is_same<BsonT, bsoncxx::types::b_array>::value    ||
                                 std::is_same<BsonT, bsoncxx::types::b_binary>::value   ||
                                 std::is_same<BsonT, bsoncxx::types::b_oid>::value      ||
                                 std::is_same<BsonT, bsoncxx::types::b_bool>::value     ||
                                 std::is_same<BsonT, bsoncxx::types::b_date>::value     ||
                                 std::is_same<BsonT, bsoncxx::types::b_int32>::value    ||
                                 std::is_same<BsonT, bsoncxx::types::b_int64>::value    ||
                                 std::is_same<BsonT, bsoncxx::oid>::value               ||
                                 std::is_same<BsonT, std::chrono::system_clock::time_point>::value;
  };

  class BSONOutputArchive : public OutputArchive<BSONOutputArchive>
  {
    enum class NodeType { StartObject, InObject, StartArray, InArray, Root };

    typedef bsoncxx::builder::core BSONBuilder;

    public:
      /*! @name Common Functionality
          Common use cases for directly interacting with an BSONOutputArchive */
      //! @{

      //! Construct, outputting to the provided stream
      /*! @param stream The stream to output to.
          @param options The JSON specific options to use.  See the Options struct
                         for the values of default parameters */
      BSONOutputArchive(std::ostream & stream) :
        OutputArchive<BSONOutputArchive>{this},
        itsBuilder{false}, // TODO: implement with "true" if top-level is array
        itsWriteStream{stream},
        itsNextName{nullptr}
      {
        itsNameCounter.push(0);
        itsNodeStack.push(NodeType::Root);
      }

      //! Destructor, flushes the JSON
      ~BSONOutputArchive()
      {
        //itsWriteStream.close();
        //itsWriteStream.write(reinterpret_cast<const char *>(itsBuilder.view_array().data()), 
        //                                                    itsBuilder.view_array().length());
        // if (itsNodeStack.top() == NodeType::InObject) {

        //   itsWriteStream.write(reinterpret_cast<const char *>(itsBuilder.view().data()), itsBuilder.view().length());

        //   //TODO  ?
        // }
      }

      //! @}
      /*! @name Internal Functionality
          Functionality designed for use by those requiring control over the inner mechanisms of
          the BSONOutputArchive */
      //! @{

      //! Starts a new node in the JSON output
      /*! The node can optionally be given a name by calling setNextName prior
          to creating the node

          Nodes only need to be started for types that are themselves objects or arrays */
      void startNode()
      {
        writeName();
        itsNodeStack.push(NodeType::StartObject);
        itsNameCounter.push(0);
      }

      //! Designates the most recently added node as finished
      void finishNode()
      {
        // if we ended up serializing an empty object or array, writeName
        // will never have been called - so start and then immediately end
        // the object/array.
        //
        // We'll also end any object/arrays we happen to be in

        bool closedObj = false;
        switch(itsNodeStack.top())
        {
          case NodeType::StartArray:
              itsBuilder.open_array();
          case NodeType::InArray:
              itsBuilder.close_array();
            break;
          case NodeType::StartObject:
              if(itsNodeStack.size() > 2) {
                itsBuilder.open_document();
              }
          case NodeType::InObject:
            if(itsNodeStack.size() > 2) {
              itsBuilder.close_document();
            }
            closedObj = true;
            break;
          default:
            break;
        }

        itsNodeStack.pop();
        itsNameCounter.pop();

        if(closedObj) { // TODO: Somehow resolve issue where non-object 
                        // values pushed to root will end up in the next 
                        // object.
          if(itsNodeStack.top() == NodeType::Root) {
            itsWriteStream.write(reinterpret_cast<const char *>(itsBuilder.view_document().data()), 
                                                                itsBuilder.view_document().length());
            itsBuilder.clear();
          }

        }
      }

      //! Sets the name for the next node created with startNode
      void setNextName( const char * name )
      {
        itsNextName = name;
      }

      // Add saveValues for every BSON type except those that 
      // are deprecated and those that are reserved for internal use
      void saveValue(bsoncxx::types::b_double d)            { itsBuilder.append(d); }
      void saveValue(bsoncxx::types::b_utf8 u)              { itsBuilder.append(u); }
      void saveValue(bsoncxx::types::b_document d)          { itsBuilder.append(d); }
      void saveValue(bsoncxx::types::b_array a)             { itsBuilder.append(a); }
      void saveValue(bsoncxx::types::b_binary b)            { itsBuilder.append(b); }
      void saveValue(bsoncxx::types::b_oid o)               { itsBuilder.append(o); }
      void saveValue(bsoncxx::oid o)                        { itsBuilder.append(o); }
      void saveValue(bsoncxx::types::b_bool b)              { itsBuilder.append(b); }
      void saveValue(bsoncxx::types::b_date d)              { itsBuilder.append(d); }
      void saveValue(bsoncxx::types::b_int32 i)             { itsBuilder.append(i); }
      void saveValue(bsoncxx::types::b_int64 i)             { itsBuilder.append(i); }

       //! Saves a datetime to the current node
      void saveValue(std::chrono::system_clock::time_point tp) { itsBuilder.append(bsoncxx::types::b_date{tp}); }

      // Add saveValues for C++ types that have a corresponding BSON type 
      //! Saves a bool to the current node
      void saveValue(bool b)                { itsBuilder.append(b); }
      //! Saves a 32 or 64 bit int to the current node
      void saveValue(std::int32_t i)        { itsBuilder.append(i); }
      void saveValue(std::int64_t i)        { itsBuilder.append(i); }

      //! Saves a double to the current node
      void saveValue(double d)              { itsBuilder.append(d); }
      //! Saves a string to the current node
      void saveValue(std::string const & s) { itsBuilder.append(s); }
      //! Saves a const char * to the current node
      void saveValue(char const * s)        { itsBuilder.append(s); }       

//     private:
//       // Some compilers/OS have difficulty disambiguating the above for various flavors of longs, so we provide
//       // special overloads to handle these cases.

//       //! 32 bit signed long saving to current node
//       template <class T, traits::EnableIf<sizeof(T) == sizeof(std::int32_t),
//                                           std::is_signed<T>::value> = traits::sfinae> inline
//       void saveLong(T l){ saveValue( static_cast<std::int32_t>( l ) ); }

//       //! non 32 bit signed long saving to current node
//       template <class T, traits::EnableIf<sizeof(T) != sizeof(std::int32_t),
//                                           std::is_signed<T>::value> = traits::sfinae> inline
//       void saveLong(T l){ saveValue( static_cast<std::int64_t>( l ) ); }

//     public:
// #ifdef _MSC_VER
//       //! MSVC only long overload to current node
//       void saveValue( unsigned long lu ){ saveLong( lu ); };
// #else // _MSC_VER
//       //! Serialize a long if it would not be caught otherwise
//       template <class T, traits::EnableIf<std::is_same<T, long>::value,
//                                           !std::is_same<T, std::int32_t>::value,
//                                           !std::is_same<T, std::int64_t>::value> = traits::sfinae> inline
//       void saveValue( T t ){ saveLong( t ); }

//       //! Serialize an unsigned long if it would not be caught otherwise
//       template <class T, traits::EnableIf<std::is_same<T, unsigned long>::value,
//                                           !std::is_same<T, std::uint32_t>::value,
//                                           !std::is_same<T, std::uint64_t>::value> = traits::sfinae> inline
//       void saveValue( T t ){ saveLong( t ); }
// #endif // _MSC_VER

      //! Write the name of the upcoming node and prepare object/array state
      /*! Since writeName is called for every value that is output, regardless of
          whether it has a name or not, it is the place where we will do a deferred
          check of our node state and decide whether we are in an array or an object.

          The general workflow of saving to the JSON archive is:

            1. (optional) Set the name for the next node to be created, usually done by an NVP
            2. Start the node
            3. (if there is data to save) Write the name of the node (this function)
            4. (if there is data to save) Save the data (with saveValue)
            5. Finish the node
          */
      void writeName()
      {
        NodeType const & nodeType = itsNodeStack.top();

        // Start up either an object or an array, depending on state
        if(nodeType == NodeType::StartArray)
        {
          itsBuilder.open_array();
          itsNodeStack.top() = NodeType::InArray;
        }
        else if(nodeType == NodeType::StartObject)
        {
          itsNodeStack.top() = NodeType::InObject;
          if(itsNodeStack.size() > 2) {
            itsBuilder.open_document();
          }
        }

        // Array types do not output names, nor does the root
        if(nodeType == NodeType::InArray) return;

        if(itsNextName == nullptr)
        {
          std::string name = "value" + std::to_string( itsNameCounter.top()++ ) + "\0";
          itsBuilder.key_owned(name);
        }
        else
        {
          itsBuilder.key_owned(itsNextName);
          itsNextName = nullptr;
        }
      }

      //! Designates that the current node should be output as an array, not an object
      void makeArray()
      {
        itsNodeStack.top() = NodeType::StartArray;
      }

      //! @}

    private:
      BSONBuilder itsBuilder; // The BSONCXX builder for this archive
      std::ostream& itsWriteStream;
      char const * itsNextName;            //!< The next name
      std::stack<uint32_t> itsNameCounter; //!< Counter for creating unique names for unnamed nodes
      std::stack<NodeType> itsNodeStack;
  }; // BSONOutputArchive


  // ######################################################################
  //! An input archive designed to load data from JSON
  /*! This archive uses RapidJSON to read in a JSON archive.

      Input JSON should have been produced by the BSONOutputArchive.  Data can
      only be added to dynamically sized containers (marked by JSON arrays) -
      the input archive will determine their size by looking at the number of child nodes.
      Only JSON originating from a BSONOutputArchive is officially supported, but data
      from other sources may work if properly formatted.

      The BSONInputArchive does not require that nodes are loaded in the same
      order they were saved by BSONOutputArchive.  Using name value pairs (NVPs),
      it is possible to load in an out of order fashion or otherwise skip/select
      specific nodes to load.

      The default behavior of the input archive is to read sequentially starting
      with the first node and exploring its children.  When a given NVP does
      not match the read in name for a node, the archive will search for that
      node at the current level and load it if it exists.  After loading an out of
      order node, the archive will then proceed back to loading sequentially from
      its new position.

      Consider this simple example where loading of some data is skipped:

      @code{cpp}
      // imagine the input file has someData(1-9) saved in order at the top level node
      ar( someData1, someData2, someData3 );        // XML loads in the order it sees in the file
      ar( cereal::make_nvp( "hello", someData6 ) ); // NVP given does not
                                                    // match expected NVP name, so we search
                                                    // for the given NVP and load that value
      ar( someData7, someData8, someData9 );        // with no NVP given, loading resumes at its
                                                    // current location, proceeding sequentially
      @endcode

      \ingroup Archives */
  class BSONInputArchive : public InputArchive<BSONInputArchive>, public traits::TextArchive
  {
    private:
      /*typedef rapidjson::GenericReadStream ReadStream;
      typedef rapidjson::GenericValue<rapidjson::UTF8<>> JSONValue;
      typedef JSONValue::ConstMemberIterator MemberIterator;
      typedef JSONValue::ConstValueIterator ValueIterator;
      typedef rapidjson::Document::GenericValue GenericValue;*/

      typedef std::vector<char> buffer_type;
      enum class NodeState { Root, InObject, InEmbeddedObject, InEmbeddedArray };

    public:
      /*! @name Common Functionality
          Common use cases for directly interacting with an BSONInputArchive */
      //! @{

      //! Construct, reading from the provided stream
      /*! @param stream The stream to read from */
      BSONInputArchive(std::istream & stream) :
        InputArchive<BSONInputArchive>(this),
        itsNextName( nullptr ),
        itsReadStream(stream)
      {
        // Add the root node to the stack.
        itsNodeStack.push(NodeState::Root);

        // Figure out how much data is in the stream.
        itsReadStream.seekg(0, itsReadStream.end);
        size_t streamLength = itsReadStream.tellg();
        itsReadStream.seekg(0, itsReadStream.beg);
      
        // Collect the BSON documents from the stream.
        while(itsReadStream.tellg() < streamLength) {

          // Determine the size of the BSON document in bytes. 
          int32_t docsize;
          char docsize_buf[4];
          itsReadStream.read(docsize_buf, sizeof(docsize));
          std::memcpy(&docsize, docsize_buf, sizeof(docsize));

          // Create a buffer to store the BSON document.
          buffer_type bsonData;
          bsonData.reserve(docsize);

          // Read the BSON data from the stream into the buffer. 
          itsReadStream.seekg(-sizeof(docsize), std::ios_base::cur);
          itsReadStream.read(&bsonData[0], docsize);
          rawBsonDocuments.push_back(std::move(bsonData));

          // Get a BSONCXX view of the document.
          bsonViews.push_back(bsoncxx::document::view{reinterpret_cast<uint8_t*>(&rawBsonDocuments.back()[0]),
                                                      static_cast<size_t>(docsize)});
        }

        curBsonView = bsonViews.begin();

        for (auto view : bsonViews) {
          std::cout << "New Doc:\n";
          for (auto e = view.begin(); e != view.end(); ++e) {
            std::cout << "Found element " << e->key() << "\n";
          }
        }
//        itsDocument.ParseStream<0>(itsReadStream);
//        itsIteratorStack.emplace_back(itsDocument.MemberBegin(), itsDocument.MemberEnd());
      }

      //! Loads some binary data, encoded as a base64 string
      /*! This will automatically start and finish a node to load the data, and can be called directly by
          users.

          Note that this follows the same ordering rules specified in the class description in regards
          to loading in/out of order */
      /*void loadBinaryValue( void * data, size_t size, const char * name = nullptr )
      {
        itsNextName = name;

        std::string encoded;
        loadValue( encoded );
        auto decoded = base64::decode( encoded );

        if( size != decoded.size() )
          throw Exception("Decoded binary data size does not match specified size");

        std::memcpy( data, decoded.data(), decoded.size() );
        itsNextName = nullptr;
      };*/

    private:
      //! @}
      /*! @name Internal Functionality
          Functionality designed for use by those requiring control over the inner mechanisms of
          the BSONInputArchive */
      //! @{

      //! An internal iterator that handles both array and object types
      /*! This class is a variant and holds both types of iterators that
          rapidJSON supports - one for arrays and one for objects. */
      // class Iterator
      // {
      //   public:
      //     Iterator() : itsIndex( 0 ), itsType(Null_) {}

      //     Iterator(MemberIterator begin, MemberIterator end) :
      //       itsMemberItBegin(begin), itsMemberItEnd(end), itsIndex(0), itsType(Member)
      //     { }

      //     Iterator(ValueIterator begin, ValueIterator end) :
      //       itsValueItBegin(begin), itsValueItEnd(end), itsIndex(0), itsType(Value)
      //     { }

      //     //! Advance to the next node
      //     Iterator & operator++()
      //     {
      //       ++itsIndex;
      //       return *this;
      //     }

      //     //! Get the value of the current node
      //     GenericValue const & value()
      //     {
      //       switch(itsType)
      //       {
      //         case Value : return itsValueItBegin[itsIndex];
      //         case Member: return itsMemberItBegin[itsIndex].value;
      //         default: throw cereal::Exception("Invalid Iterator Type!");
      //       }
      //     }

      //     //! Get the name of the current node, or nullptr if it has no name
      //     const char * name() const
      //     {
      //       if( itsType == Member && (itsMemberItBegin + itsIndex) != itsMemberItEnd )
      //         return itsMemberItBegin[itsIndex].name.GetString();
      //       else
      //         return nullptr;
      //     }

      //     //! Adjust our position such that we are at the node with the given name
      //     /*! @throws Exception if no such named node exists */
      //     inline void search( const char * searchName )
      //     {
      //       const auto len = std::strlen( searchName );
      //       size_t index = 0;
      //       for( auto it = itsMemberItBegin; it != itsMemberItEnd; ++it, ++index )
      //       {
      //         const auto currentName = it->name.GetString();
      //         if( ( std::strncmp( searchName, currentName, len ) == 0 ) &&
      //             ( std::strlen( currentName ) == len ) )
      //         {
      //           itsIndex = index;
      //           return;
      //         }
      //       }

      //       throw Exception("JSON Parsing failed - provided NVP not found");
      //     }

      //   private:
      //     MemberIterator itsMemberItBegin, itsMemberItEnd; //!< The member iterator (object)
      //     ValueIterator itsValueItBegin, itsValueItEnd;    //!< The value iterator (array)
      //     size_t itsIndex;                                 //!< The current index of this iterator
      //     enum Type {Value, Member, Null_} itsType;    //!< Whether this holds values (array) or members (objects) or nothing
      // };

      //! Searches for the expectedName node if it doesn't match the actualName
      /*! This needs to be called before every load or node start occurs.  This function will
          check to see if an NVP has been provided (with setNextName) and if so, see if that name matches the actual
          next name given.  If the names do not match, it will search in the current level of the JSON for that name.
          If the name is not found, an exception will be thrown.

          Resets the NVP name after called.

          @throws Exception if an expectedName is given and not found */

      // TODO: figure out why array::element privately inherits
      inline std::unique_ptr<bsoncxx::document::element> search()
      {
        
        std::unique_ptr<bsoncxx::document::element> elem;

        // The name an NVP provided with setNextName()
        if( itsNextName )
        {
          // If we're in an object in the Root (InObject), 
          // look for the key in the current BSON view.
          if(itsNodeStack.top() == NodeState::InObject) {
            auto elemFromView = (*curBsonView)[itsNextName];

            // Provide an error message if the key is not found.
            if(!elemFromView) {
                std::string error_msg;
                error_msg += "No element found with the key ";
                error_msg += itsNextName;
                error_msg += ".";
                throw cereal::Exception(error_msg);
            }

            elem.reset(new bsoncxx::document::element(elemFromView));

            itsNextName = nullptr;

            return elem;

          }

          // TODO: figure out a way to do this without copy/pasting code?
          //       one option is having this return a unique pointer
          //
          // If we're in an embedded object, look for the key at the top of 
          // the embedded object stack.
          if(itsNodeStack.top() == NodeState::InEmbeddedObject) {
            auto elemFromView = embeddedBsonDocStack.top()[itsNextName];

            // Provide an error message if the key is not found.
            if(!elemFromView) {
                std::string error_msg;
                error_msg += "No element found with the key ";
                error_msg += itsNextName;
                error_msg += ".";
                throw cereal::Exception(error_msg);
            }

            elem.reset(new bsoncxx::document::element(elemFromView));

            itsNextName = nullptr;

            return elem;

          }
        } else if ( itsNodeStack.top() == NodeState::InEmbeddedArray) {
            auto elemFromView = *(embeddedBsonArrayIteratorStack.top());
            ++(embeddedBsonArrayIteratorStack.top());

            if(!elemFromView) {
                throw cereal::Exception("Invalid element found in array, or array is out of bounds.\n");
            }

            elem.reset(new bsoncxx::document::element(
              elemFromView.raw(), 
              elemFromView.length(),
              elemFromView.offset()));

            return elem;
        }

        // Return an invalid element.
        // TODO: Maybe this should be an exception.
        return std::unique_ptr<bsoncxx::document::element>(new bsoncxx::document::element());

      }

    public:
      //! Starts a new node, going into its proper iterator
      /*! This places an iterator for the next node to be parsed onto the iterator stack.  If the next
          node is an array, this will be a value iterator, otherwise it will be a member iterator.

          By default our strategy is to start with the document root node and then recursively iterate through
          all children in the order they show up in the document.
          We don't need to know NVPs to do this; we'll just blindly load in the order things appear in.

          If we were given an NVP, we will search for it if it does not match our the name of the next node
          that would normally be loaded.  This functionality is provided by search(). */
      void startNode()
      { 
        if(itsNextName != nullptr) {
          std::cout << "Starting node " << itsNextName << "\n";  
        } else if(itsNodeStack.top() == NodeState::Root) {
          std::cout << "Starting root node.\n";
        } else {
          std::cout << "Starting node within array.\n";
        }
        if(itsNodeStack.top() == NodeState::InObject || 
           itsNodeStack.top() == NodeState::InEmbeddedObject ||
           itsNodeStack.top() == NodeState::InEmbeddedArray) {

            auto newNode = search();

            if(newNode->type() == bsoncxx::type::k_document) {
                std::cout << "Found document in node.\n";
                embeddedBsonDocStack.push(newNode->get_document().value);
                itsNodeStack.push(NodeState::InEmbeddedObject);
            } else if (newNode->type() == bsoncxx::type::k_array) {
                std::cout << "Found array in node.\n";
                embeddedBsonArrayStack.push(newNode->get_array().value);

                embeddedBsonArrayIteratorStack.push(embeddedBsonArrayStack.top().begin());
                itsNodeStack.push(NodeState::InEmbeddedArray);
            } else {
                throw cereal::Exception("Node requested is neither document nor array.\n");
            }

        } else if(itsNodeStack.top() == NodeState::Root) {
            itsNodeStack.push(NodeState::InObject);  
        }

        /*if(itsIteratorStack.back().value().IsArray())
          itsIteratorStack.emplace_back(itsIteratorStack.back().value().Begin(), itsIteratorStack.back().value().End());
        else
          itsIteratorStack.emplace_back(itsIteratorStack.back().value().MemberBegin(), itsIteratorStack.back().value().MemberEnd());
          */
      }

      //! Finishes the most recently started node
      void finishNode()
      {
        // If we're in an embedded object or array, pop it from its respective stack(s).
        if(itsNodeStack.top() == NodeState::InEmbeddedObject) {
          embeddedBsonDocStack.pop();
        } else if(itsNodeStack.top() == NodeState::InEmbeddedArray) {
          embeddedBsonArrayStack.pop();
          embeddedBsonArrayIteratorStack.pop();
        }

        // Pop the node type from the stack.
        itsNodeStack.pop();

        // If we're now in Root, go to the next BSON document
        if(itsNodeStack.top() == NodeState::Root) {
          ++curBsonView;
        }

        //itsIteratorStack.pop_back();
        //++itsIteratorStack.back();
      }

      //! Retrieves the current node name
      /*! @return nullptr if no name exists */
      // const char * getNodeName() const
      // {
      //   return itsIteratorStack.back().name();
      // }

      //! Sets the name for the next node created with startNode
      void setNextName( const char * name )
      {
        itsNextName = name;
      }

      // TODO: See if we can get rid of this.
      //! Loads a value from the current node - small signed overload
      template <class T, traits::EnableIf<std::is_signed<T>::value,
                                          sizeof(T) < sizeof(int64_t)> = traits::sfinae> inline
      void loadValue(T & val)
      {
        val = search()->get_int32();
      }

      // //! Loads a value from the current node - small unsigned overload
      // template <class T, traits::EnableIf<std::is_unsigned<T>::value,
      //                                     sizeof(T) < sizeof(uint64_t),
      //                                     !std::is_same<bool, T>::value> = traits::sfinae> inline
      // void loadValue(T & val)
      // {
      //   search();

      //   val = static_cast<T>( itsIteratorStack.back().value().GetUint() );
      //   ++itsIteratorStack.back();
      // }

      // TODO: this can also just be handled by BSONCXX
      // void assert_type(bsoncxx::element, bsoncxx::type t) {
      //   if(curElement.type() != t) {
      //     throw cereal::Exception("Type mismatch between requested value and variable in which to load the value.")
      //   }
      // }

      void loadValue(bsoncxx::types::b_double & val) 
      { 
        val = search()->get_double();
      }

      void loadValue(bsoncxx::types::b_utf8 & val) 
      { 
        val = search()->get_utf8();
      }

      void loadValue(bsoncxx::types::b_document & val) 
      { 
        val = search()->get_document();
      }

      void loadValue(bsoncxx::types::b_array &val) {
        val = search()->get_array();
      }
      void loadValue(bsoncxx::types::b_binary &val) {
        val = search()->get_binary();
      }
      void loadValue(bsoncxx::types::b_oid &val) {
        val.value = search()->get_oid().value;
      }
      void loadValue(bsoncxx::oid &val) {
        val = search()->get_oid().value;
      }
      void loadValue(bsoncxx::types::b_bool &val) {
        val = search()->get_bool();
      }
      void loadValue(bsoncxx::types::b_date &val) {
        val = search()->get_date();
      }
      void loadValue(bsoncxx::types::b_int32 &val) {
        val = search()->get_int32();
      }
      void loadValue(bsoncxx::types::b_int64 &val) {
        val = search()->get_int64();
      }

      //! Loads a datetime from the current node
      void loadValue(std::chrono::system_clock::time_point & val) { 
        val = search()->get_date();
      }

      // loadValues for C++ types that have a corresponding BSON type 
      void loadValue(bool & val) 
      { 
        val = search()->get_bool();
      }
      //! Loads a 32 or 64 bit int from the current node
      void loadValue(std::int32_t & val) 
      { 
        val = search()->get_int32();
      }
      void loadValue(std::int64_t & val) 
      { 
        val = search()->get_int64();
      }

      //! Loads a double from the current node
      void loadValue(double & val) 
      { 
        val = search()->get_double();
      }

      // Loads a string from the current node
      void loadValue(std::string & val) {
        val = search()->get_utf8().value.to_string();
      }

      // Special cases to handle various flavors of long, which tend to conflict with
      // the int32_t or int64_t on various compiler/OS combinations.  MSVC doesn't need any of this.
    //   #ifndef _MSC_VER
    // private:
    //   //! 32 bit signed long loading from current node
    //   template <class T> inline
    //   typename std::enable_if<sizeof(T) == sizeof(std::int32_t) && std::is_signed<T>::value, void>::type
    //   loadLong(T & l){ loadValue( reinterpret_cast<std::int32_t&>( l ) ); }

    //   //! non 32 bit signed long loading from current node
    //   template <class T> inline
    //   typename std::enable_if<sizeof(T) == sizeof(std::int64_t) && std::is_signed<T>::value, void>::type
    //   loadLong(T & l){ loadValue( reinterpret_cast<std::int64_t&>( l ) ); }

    //   //! 32 bit unsigned long loading from current node
    //   template <class T> inline
    //   typename std::enable_if<sizeof(T) == sizeof(std::uint32_t) && !std::is_signed<T>::value, void>::type
    //   loadLong(T & lu){ loadValue( reinterpret_cast<std::uint32_t&>( lu ) ); }

    //   //! non 32 bit unsigned long loading from current node
    //   template <class T> inline
    //   typename std::enable_if<sizeof(T) == sizeof(std::uint64_t) && !std::is_signed<T>::value, void>::type
    //   loadLong(T & lu){ loadValue( reinterpret_cast<std::uint64_t&>( lu ) ); }
            
    // public:
    //   //! Serialize a long if it would not be caught otherwise
    //   template <class T> inline
    //   typename std::enable_if<std::is_same<T, long>::value &&
    //                           !std::is_same<T, std::int32_t>::value &&
    //                           !std::is_same<T, std::int64_t>::value, void>::type
    //   loadValue( T & t ){ loadLong(t); }

    //   //! Serialize an unsigned long if it would not be caught otherwise
    //   template <class T> inline
    //   typename std::enable_if<std::is_same<T, unsigned long>::value &&
    //                           !std::is_same<T, std::uint32_t>::value &&
    //                           !std::is_same<T, std::uint64_t>::value, void>::type
    //   loadValue( T & t ){ loadLong(t); }
    //   #endif // _MSC_VER

    private:
      //! Convert a string to a long long
      //void stringToNumber( std::string const & str, long long & val ) { val = std::stoll( str ); }
      //! Convert a string to an unsigned long long
      //void stringToNumber( std::string const & str, unsigned long long & val ) { val = std::stoull( str ); }
      //! Convert a string to a long double
      //void stringToNumber( std::string const & str, long double & val ) { val = std::stold( str ); }

    public:
      //! Loads a value from the current node - long double and long long overloads
      // template <class T, traits::EnableIf<std::is_arithmetic<T>::value,
      //                                     !std::is_same<T, long>::value,
      //                                     !std::is_same<T, unsigned long>::value,
      //                                     !std::is_same<T, std::int64_t>::value,
      //                                     !std::is_same<T, std::uint64_t>::value,
      //                                     (sizeof(T) >= sizeof(long double) || sizeof(T) >= sizeof(long long))> = traits::sfinae>
      // inline void loadValue(T & val)
      // {
      //   std::string encoded;
      //   loadValue( encoded );
      //   stringToNumber( encoded, val );
      // }

      //! Loads the size for a SizeTag
      void loadSize(size_type & size)
      {
        if(itsNodeStack.top() != NodeState::InEmbeddedArray) {
            throw cereal::Exception("Requesting a size tag when not in an array.\n");
        }
        std::cout << "Attempting to load size.\n";
        size = std::distance(embeddedBsonArrayStack.top().begin(), 
                             embeddedBsonArrayStack.top().end());
        std::cout << "Size loaded is " << size << ".\n";
      }

      //! @}

    private:
      const char * itsNextName;               //!< Next name set by NVP
      std::istream& itsReadStream;               //!< Rapidjson write stream
      //std::vector<Iterator> itsIteratorStack; //!< 'Stack' of rapidJSON iterators

      std::vector<buffer_type> rawBsonDocuments;
      std::vector<bsoncxx::document::view> bsonViews;
      //std::vector<bsoncxx::document::iterator> itsIteratorStack;

      std::vector<bsoncxx::document::view>::iterator curBsonView;
      std::stack<bsoncxx::document::view> embeddedBsonDocStack;
      std::stack<bsoncxx::array::view> embeddedBsonArrayStack;
      std::stack<bsoncxx::array::view::iterator> embeddedBsonArrayIteratorStack;

      //bsoncxx::document::element curElement;

      std::stack<NodeState> itsNodeStack;

      //rapidjson::Document itsDocument;        //!< Rapidjson document
      //std::vector<bsoncxx::value> itsDocuments; // The BSON documents being loaded 

  };

  // ######################################################################
  // BSONArchive prologue and epilogue functions
  // ######################################################################

  // ######################################################################
  //! Prologue for NVPs for BSON output archives
  /*! NVPs do not start or finish nodes - they just set up the names */
  template <class T> inline
  void prologue( BSONOutputArchive &, NameValuePair<T> const & )
  { }

  //! Prologue for NVPs for BSON input archives
  template <class T> inline
  void prologue( BSONInputArchive &, NameValuePair<T> const & )
  { }

  // ######################################################################
  //! Epilogue for NVPs for BSON output archives
  /*! NVPs do not start or finish nodes - they just set up the names */
  template <class T> inline
  void epilogue( BSONOutputArchive &, NameValuePair<T> const & )
  { }

  // //! Epilogue for NVPs for BSON input archives
  /*! NVPs do not start or finish nodes - they just set up the names */
  template <class T> inline
  void epilogue( BSONInputArchive &, NameValuePair<T> const & )
  { }

  // ######################################################################
  //! Prologue for SizeTags for BSON output archives
  /*! SizeTags are strictly ignored for BSON, they just indicate
      that the current node should be made into an array */
  template <class T> inline
  void prologue( BSONOutputArchive & ar, SizeTag<T> const & )
  {
    ar.makeArray();
  }

  //! Prologue for SizeTags for BSON input archives
  template <class T> inline
  void prologue( BSONInputArchive &, SizeTag<T> const & )
  { }

  // ######################################################################
  //! Epilogue for SizeTags for BSON output archives
  /*! SizeTags are strictly ignored for BSON */
  template <class T> inline
  void epilogue( BSONOutputArchive &, SizeTag<T> const & )
  { }

  //! Epilogue for SizeTags for BSON input archives
  template <class T> inline
  void epilogue( BSONInputArchive &, SizeTag<T> const & )
  { }

  // ######################################################################
  //! Prologue and Epilogue for BSON types, which should not be confused 
  //  as objects or arrays

  template<class BsonT, traits::EnableIf<is_bson<BsonT>::value> = traits::sfinae>
  inline void prologue( BSONOutputArchive & ar, BsonT const &)
  {
    ar.writeName(); 
  }

  template<class BsonT, traits::EnableIf<is_bson<BsonT>::value> = traits::sfinae>
  inline void epilogue( BSONOutputArchive &, BsonT const &)
  { }

  template<class BsonT, traits::EnableIf<is_bson<BsonT>::value> = traits::sfinae>
  inline void prologue( BSONInputArchive & ar, BsonT const &)
  { }

  template<class BsonT, traits::EnableIf<is_bson<BsonT>::value> = traits::sfinae>
  inline void epilogue( BSONInputArchive &, BsonT const &)
  { }


  // ######################################################################
  //! Prologue for all other types for BSON output archives (except minimal types)
  /*! Starts a new node, named either automatically or by some NVP,
      that may be given data by the type about to be archived

      Minimal types do not start or finish nodes */
  template <class T, traits::DisableIf<std::is_arithmetic<T>::value ||
                                       traits::has_minimal_base_class_serialization<T, traits::has_minimal_output_serialization, BSONOutputArchive>::value ||
                                       traits::has_minimal_output_serialization<T, BSONOutputArchive>::value || 
                                       is_bson<T>::value> = traits::sfinae>
  inline void prologue( BSONOutputArchive & ar, T const & )
  {
    ar.startNode();
  }

  // //! Prologue for all other types for BSON input archives
  template <class T, traits::DisableIf<std::is_arithmetic<T>::value ||
                                       traits::has_minimal_base_class_serialization<T, traits::has_minimal_input_serialization, BSONInputArchive>::value ||
                                       traits::has_minimal_input_serialization<T, BSONInputArchive>::value ||
                                       is_bson<T>::value> = traits::sfinae>
  inline void prologue( BSONInputArchive & ar, T const & )
  {
    ar.startNode();
  }

  // ######################################################################
  //! Epilogue for all other types other for BSON output archives (except minimal types
  /*! Finishes the node created in the prologue

      Minimal types do not start or finish nodes */
  template <class T, traits::DisableIf<std::is_arithmetic<T>::value ||
                                       traits::has_minimal_base_class_serialization<T, traits::has_minimal_output_serialization, BSONOutputArchive>::value ||
                                       traits::has_minimal_output_serialization<T, BSONOutputArchive>::value ||
                                       is_bson<T>::value> = traits::sfinae>
  inline void epilogue( BSONOutputArchive & ar, T const & )
  {
    ar.finishNode();
  }

  // //! Epilogue for all other types other for BSON input archives
  template <class T, traits::DisableIf<std::is_arithmetic<T>::value ||
                                       traits::has_minimal_base_class_serialization<T, traits::has_minimal_input_serialization, BSONInputArchive>::value ||
                                       traits::has_minimal_input_serialization<T, BSONInputArchive>::value ||
                                       is_bson<T>::value> = traits::sfinae>
  inline void epilogue( BSONInputArchive & ar, T const & )
  {
    ar.finishNode();
  }

  // ######################################################################
  //! Prologue for arithmetic types for BSON output archives
  template <class T, traits::EnableIf<std::is_arithmetic<T>::value> = traits::sfinae> inline
  void prologue( BSONOutputArchive & ar, T const & )
  {
    ar.writeName();
  }

  //! Prologue for arithmetic types for BSON input archives
  template <class T, traits::EnableIf<std::is_arithmetic<T>::value> = traits::sfinae> inline
  void prologue( BSONInputArchive &, T const & )
  { }

  // ######################################################################
  //! Epilogue for arithmetic types for BSON output archives
  template <class T, traits::EnableIf<std::is_arithmetic<T>::value> = traits::sfinae> inline
  void epilogue( BSONOutputArchive &, T const & )
  { }

  //! Epilogue for arithmetic types for BSON input archives
  template <class T, traits::EnableIf<std::is_arithmetic<T>::value> = traits::sfinae> inline
  void epilogue( BSONInputArchive &, T const & )
  { }

  // ######################################################################
  //! Prologue for strings for BSON output archives
  template<class CharT, class Traits, class Alloc> inline
  void prologue(BSONOutputArchive & ar, std::basic_string<CharT, Traits, Alloc> const &)
  {
    ar.writeName();
  }

  //! Prologue for strings for BSON input archives
  template<class CharT, class Traits, class Alloc> inline
  void prologue(BSONInputArchive &, std::basic_string<CharT, Traits, Alloc> const &)
  { }

  // ######################################################################
  //! Epilogue for strings for BSON output archives
  template<class CharT, class Traits, class Alloc> inline
  void epilogue(BSONOutputArchive &, std::basic_string<CharT, Traits, Alloc> const &)
  { }

  //! Epilogue for strings for BSON output archives
  template<class CharT, class Traits, class Alloc> inline
  void epilogue(BSONInputArchive &, std::basic_string<CharT, Traits, Alloc> const &)
  { }

  // ######################################################################
  // Common JSONArchive serialization functions
  // ######################################################################
  //! Serializing NVP types to BSON
  template <class T> inline
  void CEREAL_SAVE_FUNCTION_NAME( BSONOutputArchive & ar, NameValuePair<T> const & t )
  {
    ar.setNextName( t.name );
    ar( t.value );
  }

  template <class T> inline
  void CEREAL_LOAD_FUNCTION_NAME( BSONInputArchive & ar, NameValuePair<T> & t )
  {
    ar.setNextName( t.name );
    ar( t.value );
  }

  //! Saving for arithmetic to JSON
  template <class T, traits::EnableIf<std::is_arithmetic<T>::value> = traits::sfinae> inline
  void CEREAL_SAVE_FUNCTION_NAME(BSONOutputArchive & ar, T const & t)
  {
    ar.saveValue( t );
  }

  //! Loading arithmetic from JSON
  template <class T, traits::EnableIf<std::is_arithmetic<T>::value> = traits::sfinae> inline
  void CEREAL_LOAD_FUNCTION_NAME(BSONInputArchive & ar, T & t)
  {
    ar.loadValue( t );
  }

  //! saving string to JSON
  template<class CharT, class Traits, class Alloc> inline
  void CEREAL_SAVE_FUNCTION_NAME(BSONOutputArchive & ar, std::basic_string<CharT, Traits, Alloc> const & str)
  {
    ar.saveValue( str );
  }

  //! loading string from JSON
  template<class CharT, class Traits, class Alloc> inline
  void CEREAL_LOAD_FUNCTION_NAME(BSONInputArchive & ar, std::basic_string<CharT, Traits, Alloc> & str)
  {
    ar.loadValue( str );
  }

  // ######################################################################
  //! Saving SizeTags to BSON
  template <class T> inline
  void CEREAL_SAVE_FUNCTION_NAME( BSONOutputArchive &, SizeTag<T> const & )
  {
    // nothing to do here, we don't explicitly save the size
  }

  //! Loading SizeTags from BSON
  template <class T> inline
  void CEREAL_LOAD_FUNCTION_NAME( BSONInputArchive & ar, SizeTag<T> & st )
  {
    ar.loadSize( st.size );
  }

  // ######################################################################
  //! Saving BSON types to BSON
  template<class BsonT, traits::EnableIf<is_bson<BsonT>::value> = traits::sfinae> inline
  void CEREAL_SAVE_FUNCTION_NAME( BSONOutputArchive& ar, BsonT const & bsonVal ) 
  {
    ar.saveValue(bsonVal);
  }

  //! Loading BSON types from BSON
  template<class BsonT, traits::EnableIf<is_bson<BsonT>::value> = traits::sfinae> inline
  void CEREAL_LOAD_FUNCTION_NAME( BSONInputArchive& ar, BsonT & bsonVal ) 
  {
    ar.loadValue(bsonVal);
  }

} // namespace cereal

// register archives for polymorphic support
CEREAL_REGISTER_ARCHIVE(cereal::BSONInputArchive)
CEREAL_REGISTER_ARCHIVE(cereal::BSONOutputArchive)

// tie input and output archives together
CEREAL_SETUP_ARCHIVE_TRAITS(cereal::BSONInputArchive, cereal::BSONOutputArchive)

#endif // CEREAL_ARCHIVES_BSON_HPP_