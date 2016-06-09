#ifndef CEREAL_ARCHIVES_BSON_HPP_
#define CEREAL_ARCHIVES_BSON_HPP_

#include <stack>
#include <string>
#include <vector>

#include <cereal/cereal.hpp>

#include <bsoncxx/builder/core.hpp>

namespace cereal {

/**
 * A templated stuct containing a bool value that specifies whether the
 * provided template parameter is a BSON type.
 */
template <class BsonT>
struct is_bson {
    static constexpr bool value = std::is_same<BsonT, bsoncxx::types::b_double>::value ||
        std::is_same<BsonT, bsoncxx::types::b_utf8>::value ||
        std::is_same<BsonT, bsoncxx::types::b_document>::value ||
        std::is_same<BsonT, bsoncxx::types::b_array>::value ||
        std::is_same<BsonT, bsoncxx::types::b_binary>::value ||
        std::is_same<BsonT, bsoncxx::types::b_oid>::value ||
        std::is_same<BsonT, bsoncxx::types::b_bool>::value ||
        std::is_same<BsonT, bsoncxx::types::b_date>::value ||
        std::is_same<BsonT, bsoncxx::types::b_int32>::value ||
        std::is_same<BsonT, bsoncxx::types::b_int64>::value ||
        std::is_same<BsonT, bsoncxx::oid>::value;
};

class BSONOutputArchive : public OutputArchive<BSONOutputArchive> {
    /**
    * The possible states for the BSON nodes being output by the archive.
    */
    enum class NodeType { Root, StartObject, InObject, StartArray, InArray };

    typedef bsoncxx::builder::core BSONBuilder;

public:
    /**
    * Construct a BSONOutputArchive that will output to the provided stream.
    *
    * @param stream
    *   The stream to which the archiver will output BSON data to.
    */
    BSONOutputArchive(std::ostream& stream)
        : OutputArchive<BSONOutputArchive>{this},
          itsBuilder{false},
          itsWriteStream{stream},
          itsNextName{nullptr} {
        itsNameCounter.push(0);
        itsNodeStack.push(NodeType::Root);
    }

    /**
     * Starts a new node in the BSON output.
     *
     * The node can optionally be given a name by calling setNextName prior to
     * creating the node. Nodes only need to be started for types that are
     * objects or arrays.
     */
    void startNode() {
        writeName();
        itsNodeStack.push(NodeType::StartObject);
        itsNameCounter.push(0);
    }

    /**
     * Designates the most recently added node as finished.
     */
    void finishNode() {
        // Iff we ended up serializing an empty object or array, writeName
        // will never have been called - so start and then immediately end
        // the object/array.
        //
        // We'll also end any object/arrays we happen to be in.

        bool closedObj = false;
        switch (itsNodeStack.top()) {
            case NodeType::StartArray:
                itsBuilder.open_array();
            case NodeType::InArray:
                itsBuilder.close_array();
                break;
            case NodeType::StartObject:
                if (itsNodeStack.size() > 2) {
                    itsBuilder.open_document();
                }
            case NodeType::InObject:
                if (itsNodeStack.size() > 2) {
                    itsBuilder.close_document();
                }
                closedObj = true;
                break;
            default:
                break;
        }

        itsNodeStack.pop();
        itsNameCounter.pop();

        // TODO: Somehow resolve issue where non-object
        // values pushed to root will end up in the next
        // object.
        if (closedObj) {
            if (itsNodeStack.top() == NodeType::Root) {
                // Write the BSON data for the document that was just completed,
                // and reset the builder for the next document.
                itsWriteStream.write(
                    reinterpret_cast<const char*>(itsBuilder.view_document().data()),
                    itsBuilder.view_document().length());
                itsBuilder.clear();
            }
        }
    }

    /**
     * Sets the name for the next node or element.
     *
     * @param name
     *    The name for the next node or element.
     */
    void setNextName(const char* name) {
        itsNextName = name;
    }

    /**
     * Implementations of saveValue that save a BSON type to the current node,
     * for every BSON type except those that are deprecated and those that are
     * reserved for internal use.
     *
     * @param val
     *    The BSON typed value to save to the archive.
     */
    template <class BsonT, traits::EnableIf<is_bson<BsonT>::value> = traits::sfinae>
    void saveValue(BsonT val) {
        itsBuilder.append(val);
    }

    /**
     * Saves a datetime to the current node.
     *
     * @param tp
     *    The timepoint to save to the archive.
     */
    void saveValue(std::chrono::system_clock::time_point tp) {
        itsBuilder.append(bsoncxx::types::b_date{tp});
    }

    /**
     * Saves a bool to the current node.
     *
     * @param b
     *    The bool to save to the archive.
     */
    void saveValue(bool b) {
        itsBuilder.append(b);
    }

    /**
     * Saves a signed 32 bit int to the current node.
     *
     * @param i
     *    The int32_t to save to the archive.
     */
    void saveValue(std::int32_t i) {
        itsBuilder.append(i);
    }

    /**
     * Saves a signed 32 bit int to the current node.
     *
     * @param i
     *    The int32_t to save to the archive.
     */
    void saveValue(std::int64_t i) {
        itsBuilder.append(i);
    }

    /**
     * Saves a double to the current node.
     *
     * @param d
     *    The double to save to the archive.
     */
    void saveValue(double d) {
        itsBuilder.append(d);
    }

    /**
     * Saves a std::string to the current node.
     *
     * @param s
     *    The std::string to save to the archive.
     */
    void saveValue(std::string const& s) {
        itsBuilder.append(s);
    }

    /**
     * Saves a c style string to the current node.
     *
     * @param s
     *    The char* string to save to the archive.
     */
    void saveValue(char const* s) {
        itsBuilder.append(s);
    }

    /**
     * Write the name of the upcoming element and prepare object/array state
     * Since writeName is called for every value that is output, regardless of
     * whether it has a name or not, it is the place where we will do a deferred
     * check of our node state and decide whether we are in an array or an object.
     *
     * The general workflow of saving to the BSON archive is:
     *   1. Set the name for the next node to be created, usually done by an NVP.
     *   2. Start the node.
     *   3. (if there is data to save) Write the name of the node (this function).
     *   4. (if there is data to save) Save each element of data (with saveValue).
     *   5. Finish the node.
     */
    void writeName() {
        NodeType const& nodeType = itsNodeStack.top();

        // Start up either an object or an array, depending on state.
        if (nodeType == NodeType::StartArray) {
            itsBuilder.open_array();
            itsNodeStack.top() = NodeType::InArray;
        } else if (nodeType == NodeType::StartObject) {
            itsNodeStack.top() = NodeType::InObject;
            if (itsNodeStack.size() > 2) {
                itsBuilder.open_document();
            }
        }

        // Elements in arrays do not have names.
        if (nodeType == NodeType::InArray)
            return;

        if (itsNextName == nullptr) {
            // Generate a unique name for this unnamed node.
            std::string name = "value" + std::to_string(itsNameCounter.top()++) + "\0";
            itsBuilder.key_owned(name);
        } else {
            // Set the key of this element to the name stored by the archiver.
            itsBuilder.key_owned(itsNextName);
            itsNextName = nullptr;
        }
    }

    /**
     * Designates that the current node should be output as an array, not an
     * object.
     */
    void makeArray() {
        itsNodeStack.top() = NodeType::StartArray;
    }

private:
    // The BSONCXX builder for this archive.
    BSONBuilder itsBuilder;

    // The stream to which to write the BSON archive.
    std::ostream& itsWriteStream;

    // The name of the next element to be added to the archive.
    char const* itsNextName;

    // Counter for creating unique names for unnamed nodes.
    std::stack<uint32_t> itsNameCounter;

    // A stack maintaining the state of the nodes currently being written.
    std::stack<NodeType> itsNodeStack;

};  // BSONOutputArchive


class BSONInputArchive : public InputArchive<BSONInputArchive>, public traits::TextArchive {
private:
    typedef std::vector<char> buffer_type;
    enum class NodeState { Root, InObject, InEmbeddedObject, InEmbeddedArray };

public:
    /**
    * Construct a BSONInputArchive from an input stream of BSON data.
    *
    * @param stream
    *    The stream from which to read BSON data.
    */
    BSONInputArchive(std::istream& stream)
        : InputArchive<BSONInputArchive>(this), itsNextName(nullptr), itsReadStream(stream) {

        // Add the root node to the stack.
        itsNodeStack.push(NodeState::Root);

        // Calculate out how much data is in the stream.
        itsReadStream.seekg(0, itsReadStream.end);
        size_t streamLength = itsReadStream.tellg();
        itsReadStream.seekg(0, itsReadStream.beg);

        // Collect every BSON documents from the stream.
        while (itsReadStream.tellg() < streamLength) {
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
            bsonViews.push_back(
                bsoncxx::document::view{reinterpret_cast<uint8_t*>(&rawBsonDocuments.back()[0]),
                                        static_cast<size_t>(docsize)});
        }

        // Set the root view iterator to the first BSON document in the stream.
        curBsonView = bsonViews.begin();
    }

private:
    /**
     * Searches for the next BSON element to be retrieved and loaded.
     *
     * @return a pointer to the element with itsNextName as a key, or if the current node is an
     *         array, the next element in that array.
     */
    inline std::unique_ptr<bsoncxx::document::element> search() {
        // Set up the return value.
        std::unique_ptr<bsoncxx::document::element> elem;

        if (itsNextName) {
            // If we're in an object in the Root (InObject),
            // look for the key in the current BSON view.
            if (itsNodeStack.top() == NodeState::InObject) {
                auto elemFromView = (*curBsonView)[itsNextName];
                elem.reset(new bsoncxx::document::element(elemFromView));
            }

            // If we're in an embedded object, look for the key in the object
            // at the top of the embedded object stack.
            if (itsNodeStack.top() == NodeState::InEmbeddedObject) {
                auto elemFromView = embeddedBsonDocStack.top()[itsNextName];
                elem.reset(new bsoncxx::document::element(elemFromView));
            }

            // Provide an error message if the key is not found.
            if (!(*elem)) {
                std::string error_msg;
                error_msg += "No element found with the key ";
                error_msg += itsNextName;
                error_msg += ".";
                throw cereal::Exception(error_msg);
            }

            // Reset the name of the next element to be retrieved.
            itsNextName = nullptr;

            return elem;

        } else if (itsNodeStack.top() == NodeState::InEmbeddedArray) {
            // If we're in an array (InEmbeddedArray), retrieve an element from
            // the array iterator at the top of the stack, and increment it for
            // the next retrieval.
            auto elemFromView = *(embeddedBsonArrayIteratorStack.top());
            ++(embeddedBsonArrayIteratorStack.top());

            if (!elemFromView) {
                throw cereal::Exception(
                    "Invalid element found in array, or array is out of bounds.\n");
            }

            // TODO: figure out why array::element privately inherits.
            // Construct a bsoncxx::document::element from the components
            // of the bsoncxx::array::element.
            elem.reset(new bsoncxx::document::element(
                elemFromView.raw(), elemFromView.length(), elemFromView.offset()));

            return elem;
        }

        // Return an invalid element.
        // TODO: Maybe this should be an exception.
        return std::unique_ptr<bsoncxx::document::element>(new bsoncxx::document::element());
    }

public:
    /**
    * Starts a new node, and update the stacks so that
    * we fetch the correct data when calling search().
    */
    void startNode() {
        // If we're not in the root node, match the next key to an embedded document
        // or array.
        if (itsNodeStack.top() == NodeState::InObject ||
            itsNodeStack.top() == NodeState::InEmbeddedObject ||
            itsNodeStack.top() == NodeState::InEmbeddedArray) {

            // From the BSON document we're currently in, fetch the value associated
            // with
            // this node and update the relevant stacks.
            auto newNode = search();
            if (newNode->type() == bsoncxx::type::k_document) {
                embeddedBsonDocStack.push(newNode->get_document().value);
                itsNodeStack.push(NodeState::InEmbeddedObject);
            } else if (newNode->type() == bsoncxx::type::k_array) {
                embeddedBsonArrayStack.push(newNode->get_array().value);
                embeddedBsonArrayIteratorStack.push(embeddedBsonArrayStack.top().begin());
                itsNodeStack.push(NodeState::InEmbeddedArray);
            } else {
                throw cereal::Exception("Node requested is neither document nor array.\n");
            }

        } else if (itsNodeStack.top() == NodeState::Root) {
            // If we are in the root node, update the state of the node we're
            // currently in,
            // but do not do anything else.
            itsNodeStack.push(NodeState::InObject);
        }
    }

    /**
     * Finishes the most recently started node by popping relevant stacks
     * and, if necessary, iterating to the next root BSON document.
     */
    void finishNode() {
        // If we're in an embedded object or array, pop it from its respective
        // stack(s).
        if (itsNodeStack.top() == NodeState::InEmbeddedObject) {
            embeddedBsonDocStack.pop();
        } else if (itsNodeStack.top() == NodeState::InEmbeddedArray) {
            embeddedBsonArrayStack.pop();
            embeddedBsonArrayIteratorStack.pop();
        }

        // Pop the node type from the stack.
        itsNodeStack.pop();

        // If we're now in Root, go to the next BSON document
        if (itsNodeStack.top() == NodeState::Root) {
            ++curBsonView;
        }
    }

    /**
     * Sets the name for the next node created with startNode
     *
     * @param name
     *  The name of the next node
     */
    void setNextName(const char* name) {
        itsNextName = name;
    }

    // TODO: this can also just be handled by BSONCXX
    // void assert_type(bsoncxx::element, bsoncxx::type t) {
    //   if(curElement.type() != t) {
    //     throw cereal::Exception("Type mismatch between requested value and
    //     variable in which to load the value.")
    //   }
    // }

    // TODO?: add support for arithmetic types that aren't int32_t, int64_t, and
    // double.

    /**
     * Loads a double from the current node.
     *
     * @param val
     *    The BSON variable into which the double will be loaded.
     */
    void loadValue(bsoncxx::types::b_double& val) {
        val = search()->get_double();
    }

    /**
     * Loads a BSON UTF-8 value from the current node.
     *
     * @param val
     *    The BSON variable into which the UTF-8 will be loaded.
     */
    void loadValue(bsoncxx::types::b_utf8& val) {
        val = search()->get_utf8();
    }

    /**
     * Loads a BSON document from the current node.
     *
     * @param val
     *    The BSON variable into which the document will be loaded.
     */
    void loadValue(bsoncxx::types::b_document& val) {
        val = search()->get_document();
    }

    /**
     * Loads a BSON array from the current node.
     *
     * @param val
     *    The BSON variable into which the array will be loaded.
     */
    void loadValue(bsoncxx::types::b_array& val) {
        val = search()->get_array();
    }

    /**
     * Loads a BSON binary data value from the current node.
     *
     * @param val
     *    The BSON variable into which the binary data will be loaded.
     */
    void loadValue(bsoncxx::types::b_binary& val) {
        val = search()->get_binary();
    }

    /**
     * Loads a BSON ObjectID from the current node.
     *
     * @param val
     *    The BSON variable into which the OID will be loaded.
     */
    void loadValue(bsoncxx::types::b_oid& val) {
        val.value = search()->get_oid().value;
    }

    /**
     * Loads an ObjectID from the current node.
     *
     * @param val
     *    The oid variable into which the OID will be loaded.
     */
    void loadValue(bsoncxx::oid& val) {
        val = search()->get_oid().value;
    }

    /**
     * Loads a bool from the current node.
     *
     * @param val
     *    The BSON variable into which the bool will be loaded.
     */
    void loadValue(bsoncxx::types::b_bool& val) {
        val = search()->get_bool();
    }

    /**
     * Loads a BSON datetime from the current node.
     *
     * @param val
     *    The BSON variable into which the datetime will be loaded.
     */
    void loadValue(bsoncxx::types::b_date& val) {
        val = search()->get_date();
    }

    /**
     * Loads a 32 bit int from the current node.
     *
     * @param val
     *    The BSON variable into which the int will be loaded.
     */
    void loadValue(bsoncxx::types::b_int32& val) {
        val = search()->get_int32();
    }

    /**
     * Loads a 64 bit int from the current node.
     *
     * @param val
     *    The BSON variable into which the int will be loaded.
     */
    void loadValue(bsoncxx::types::b_int64& val) {
        val = search()->get_int64();
    }

    /**
     * Loads a BSON datetime from the current node.
     *
     * @param val
     *    The time_point variable into which the datetime will be loaded.
     */
    void loadValue(std::chrono::system_clock::time_point& val) {
        val = search()->get_date();
    }

    /**
     * Loads a bool from the current node.
     *
     * @param val
     *    The boolean variable into which the bool will be loaded.
     */
    void loadValue(bool& val) {
        val = search()->get_bool();
    }

    /**
     * Loads a 32 bit int from the current node.
     *
     * @param val
     *    The int32_t variable into which the int will be loaded.
     */
    void loadValue(std::int32_t& val) {
        val = search()->get_int32();
    }

    /**
     * Loads a 64 bit int from the current node.
     *
     * @param val
     *    The int64_t variable into which the int will be loaded.
     */
    void loadValue(std::int64_t& val) {
        val = search()->get_int64();
    }

    /**
     * Loads a double from the current node.
     *
     * @param val
     *    The double variable into which the double will be loaded.
     */
    void loadValue(double& val) {
        val = search()->get_double();
    }

    /**
     * Loads a BSON UTF-8 value from the current node.
     *
     * @param val
     *    The std::string variable into which the UTF-8 will be loaded.
     */
    void loadValue(std::string& val) {
        val = search()->get_utf8().value.to_string();
    }

public:
    /**
     * Loads the size for a SizeTag, which is used by Cereal to determine how many
     * elements to put into a container such as a std::vector.
     *
     * @param size
     *  A reference ot the size value that will be set to the size of the array at the tiop of the
     * stack.
     */
    void loadSize(size_type& size) {
        if (itsNodeStack.top() != NodeState::InEmbeddedArray) {
            throw cereal::Exception("Requesting a size tag when not in an array.\n");
        }
        size =
            std::distance(embeddedBsonArrayStack.top().begin(), embeddedBsonArrayStack.top().end());
    }

private:
    // The key name of the next element being searched.
    const char* itsNextName;

    // The stream of BSON being read.
    std::istream& itsReadStream;

    // The raw BSON data read from the stream.
    std::vector<buffer_type> rawBsonDocuments;

    // The bsoncxx document views of the raw BSON data.
    std::vector<bsoncxx::document::view> bsonViews;

    // The current root BSON document being viewed.
    std::vector<bsoncxx::document::view>::iterator curBsonView;

    // Stack maintaining views of embedded BSON documents.
    std::stack<bsoncxx::document::view> embeddedBsonDocStack;

    // Stacks maintaining views of embedded BSON arrays, as well as their
    // iterators.
    std::stack<bsoncxx::array::view> embeddedBsonArrayStack;
    std::stack<bsoncxx::array::view::iterator> embeddedBsonArrayIteratorStack;

    // A stack maintaining the state of the node currently being worked on.
    std::stack<NodeState> itsNodeStack;

};  // BSONInputArchive

// ######################################################################
// BSONArchive prologue and epilogue functions
// ######################################################################

// ######################################################################
// Prologue for NVPs for BSON output archives
// NVPs do not start or finish nodes - they just set up the names
template <class T>
inline void prologue(BSONOutputArchive&, NameValuePair<T> const&) {}

// Prologue for NVPs for BSON input archives
template <class T>
inline void prologue(BSONInputArchive&, NameValuePair<T> const&) {}

// ######################################################################
// Epilogue for NVPs for BSON output archives
// NVPs do not start or finish nodes - they just set up the names
template <class T>
inline void epilogue(BSONOutputArchive&, NameValuePair<T> const&) {}

// Epilogue for NVPs for BSON input archives
// NVPs do not start or finish nodes - they just set up the names
template <class T>
inline void epilogue(BSONInputArchive&, NameValuePair<T> const&) {}

// ######################################################################
// Prologue for SizeTags for BSON output archives
// SizeTags are strictly ignored for BSON, they just indicate
// that the current node should be made into an array
template <class T>
inline void prologue(BSONOutputArchive& ar, SizeTag<T> const&) {
    ar.makeArray();
}

// Prologue for SizeTags for BSON input archives
template <class T>
inline void prologue(BSONInputArchive&, SizeTag<T> const&) {}

// ######################################################################
// Epilogue for SizeTags for BSON output archives
// SizeTags are strictly ignored for BSON
template <class T>
inline void epilogue(BSONOutputArchive&, SizeTag<T> const&) {}

// Epilogue for SizeTags for BSON input archives
template <class T>
inline void epilogue(BSONInputArchive&, SizeTag<T> const&) {}

// ######################################################################
// Prologue and Epilogue for BSON types, which should not be confused
// as objects or arrays

template <class BsonT,
          traits::EnableIf<is_bson<BsonT>::value ||
                           std::is_same<BsonT, std::chrono::system_clock::time_point>::value> =
              traits::sfinae>
inline void prologue(BSONOutputArchive& ar, BsonT const&) {
    ar.writeName();
}

template <class BsonT,
          traits::EnableIf<is_bson<BsonT>::value ||
                           std::is_same<BsonT, std::chrono::system_clock::time_point>::value> =
              traits::sfinae>
inline void epilogue(BSONOutputArchive&, BsonT const&) {}

template <class BsonT,
          traits::EnableIf<is_bson<BsonT>::value ||
                           std::is_same<BsonT, std::chrono::system_clock::time_point>::value> =
              traits::sfinae>
inline void prologue(BSONInputArchive& ar, BsonT const&) {}

template <class BsonT,
          traits::EnableIf<is_bson<BsonT>::value ||
                           std::is_same<BsonT, std::chrono::system_clock::time_point>::value> =
              traits::sfinae>
inline void epilogue(BSONInputArchive&, BsonT const&) {}

// ######################################################################
// Prologue for all other types for BSON output archives (except minimal types)
// Starts a new node, named either automatically or by some NVP,
// that may be given data by the type about to be archived

// Minimal types do not start or finish nodes
template <class T,
          traits::DisableIf<
              std::is_arithmetic<T>::value ||
              traits::has_minimal_base_class_serialization<T,
                                                           traits::has_minimal_output_serialization,
                                                           BSONOutputArchive>::value ||
              traits::has_minimal_output_serialization<T, BSONOutputArchive>::value ||
              is_bson<T>::value || std::is_same<T, std::chrono::system_clock::time_point>::value> =
              traits::sfinae>
inline void prologue(BSONOutputArchive& ar, T const&) {
    ar.startNode();
}

// Prologue for all other types for BSON input archives
template <
    class T,
    traits::DisableIf<
        std::is_arithmetic<T>::value ||
        traits::has_minimal_base_class_serialization<T,
                                                     traits::has_minimal_input_serialization,
                                                     BSONInputArchive>::value ||
        traits::has_minimal_input_serialization<T, BSONInputArchive>::value || is_bson<T>::value ||
        std::is_same<T, std::chrono::system_clock::time_point>::value> = traits::sfinae>
inline void prologue(BSONInputArchive& ar, T const&) {
    ar.startNode();
}

// ######################################################################
// Epilogue for all other types other for BSON output archives (except minimal
// types
// Finishes the node created in the prologue

// Minimal types do not start or finish nodes
template <class T,
          traits::DisableIf<
              std::is_arithmetic<T>::value ||
              traits::has_minimal_base_class_serialization<T,
                                                           traits::has_minimal_output_serialization,
                                                           BSONOutputArchive>::value ||
              traits::has_minimal_output_serialization<T, BSONOutputArchive>::value ||
              is_bson<T>::value || std::is_same<T, std::chrono::system_clock::time_point>::value> =
              traits::sfinae>
inline void epilogue(BSONOutputArchive& ar, T const&) {
    ar.finishNode();
}

// Epilogue for all other types other for BSON input archives
template <
    class T,
    traits::DisableIf<
        std::is_arithmetic<T>::value ||
        traits::has_minimal_base_class_serialization<T,
                                                     traits::has_minimal_input_serialization,
                                                     BSONInputArchive>::value ||
        traits::has_minimal_input_serialization<T, BSONInputArchive>::value || is_bson<T>::value ||
        std::is_same<T, std::chrono::system_clock::time_point>::value> = traits::sfinae>
inline void epilogue(BSONInputArchive& ar, T const&) {
    ar.finishNode();
}

// ######################################################################
// Prologue for arithmetic types for BSON output archives
template <class T, traits::EnableIf<std::is_arithmetic<T>::value> = traits::sfinae>
inline void prologue(BSONOutputArchive& ar, T const&) {
    ar.writeName();
}

// Prologue for arithmetic types for BSON input archives
template <class T, traits::EnableIf<std::is_arithmetic<T>::value> = traits::sfinae>
inline void prologue(BSONInputArchive&, T const&) {}

// ######################################################################
// Epilogue for arithmetic types for BSON output archives
template <class T, traits::EnableIf<std::is_arithmetic<T>::value> = traits::sfinae>
inline void epilogue(BSONOutputArchive&, T const&) {}

// Epilogue for arithmetic types for BSON input archives
template <class T, traits::EnableIf<std::is_arithmetic<T>::value> = traits::sfinae>
inline void epilogue(BSONInputArchive&, T const&) {}

// ######################################################################
// Prologue for strings for BSON output archives
template <class CharT, class Traits, class Alloc>
inline void prologue(BSONOutputArchive& ar, std::basic_string<CharT, Traits, Alloc> const&) {
    ar.writeName();
}

// Prologue for strings for BSON input archives
template <class CharT, class Traits, class Alloc>
inline void prologue(BSONInputArchive&, std::basic_string<CharT, Traits, Alloc> const&) {}

// ######################################################################
// Epilogue for strings for BSON output archives
template <class CharT, class Traits, class Alloc>
inline void epilogue(BSONOutputArchive&, std::basic_string<CharT, Traits, Alloc> const&) {}

// Epilogue for strings for BSON output archives
template <class CharT, class Traits, class Alloc>
inline void epilogue(BSONInputArchive&, std::basic_string<CharT, Traits, Alloc> const&) {}

// ######################################################################
// Common BSONArchive serialization functions
// ######################################################################
// Serializing NVP types to BSON
template <class T>
inline void CEREAL_SAVE_FUNCTION_NAME(BSONOutputArchive& ar, NameValuePair<T> const& t) {
    ar.setNextName(t.name);
    ar(t.value);
}

template <class T>
inline void CEREAL_LOAD_FUNCTION_NAME(BSONInputArchive& ar, NameValuePair<T>& t) {
    ar.setNextName(t.name);
    ar(t.value);
}

// Saving for arithmetic to BSON
template <class T, traits::EnableIf<std::is_arithmetic<T>::value> = traits::sfinae>
inline void CEREAL_SAVE_FUNCTION_NAME(BSONOutputArchive& ar, T const& t) {
    ar.saveValue(t);
}

// Loading arithmetic from BSON
template <class T, traits::EnableIf<std::is_arithmetic<T>::value> = traits::sfinae>
inline void CEREAL_LOAD_FUNCTION_NAME(BSONInputArchive& ar, T& t) {
    ar.loadValue(t);
}

// saving string to BSON
template <class CharT, class Traits, class Alloc>
inline void CEREAL_SAVE_FUNCTION_NAME(BSONOutputArchive& ar,
                                      std::basic_string<CharT, Traits, Alloc> const& str) {
    ar.saveValue(str);
}

// loading string from BSON
template <class CharT, class Traits, class Alloc>
inline void CEREAL_LOAD_FUNCTION_NAME(BSONInputArchive& ar,
                                      std::basic_string<CharT, Traits, Alloc>& str) {
    ar.loadValue(str);
}

// ######################################################################
// Saving SizeTags to BSON
template <class T>
inline void CEREAL_SAVE_FUNCTION_NAME(BSONOutputArchive&, SizeTag<T> const&) {
    // Nothing to do here, we don't explicitly save the size.
}

// Loading SizeTags from BSON
template <class T>
inline void CEREAL_LOAD_FUNCTION_NAME(BSONInputArchive& ar, SizeTag<T>& st) {
    ar.loadSize(st.size);
}

// ######################################################################
// Saving BSON types to BSON
template <class BsonT,
          traits::EnableIf<is_bson<BsonT>::value ||
                           std::is_same<BsonT, std::chrono::system_clock::time_point>::value> =
              traits::sfinae>
inline void CEREAL_SAVE_FUNCTION_NAME(BSONOutputArchive& ar, BsonT const& bsonVal) {
    ar.saveValue(bsonVal);
}

// Loading BSON types from BSON
template <class BsonT,
          traits::EnableIf<is_bson<BsonT>::value ||
                           std::is_same<BsonT, std::chrono::system_clock::time_point>::value> =
              traits::sfinae>
inline void CEREAL_LOAD_FUNCTION_NAME(BSONInputArchive& ar, BsonT& bsonVal) {
    ar.loadValue(bsonVal);
}

}  // namespace cereal

// Register archives for polymorphic support.
CEREAL_REGISTER_ARCHIVE(cereal::BSONInputArchive)
CEREAL_REGISTER_ARCHIVE(cereal::BSONOutputArchive)

// Tie input and output archives together.
CEREAL_SETUP_ARCHIVE_TRAITS(cereal::BSONInputArchive, cereal::BSONOutputArchive)

#endif  // CEREAL_ARCHIVES_BSON_HPP_