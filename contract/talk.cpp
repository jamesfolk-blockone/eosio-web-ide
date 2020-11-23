#include <eosio/eosio.hpp>

using reaction_type_type = uint8_t;

// tally of available reactions
struct reaction_tally {

    enum class type { thumbs_up=0, thumbs_down };
    uint64_t thumbs_up {0};
    uint64_t thumbs_down {0};

    reaction_tally &operator+=(reaction_type_type r) {
        switch((type)r) {
            case type::thumbs_up: thumbs_up++; break;
            case type::thumbs_down: thumbs_down++; break;
        }
        return *this;
    }

    reaction_tally &operator-=(reaction_type_type r) {
        switch((type)r) {
            case type::thumbs_up: thumbs_up--; break;
            case type::thumbs_down: thumbs_down--; break;
        }
        return *this;
    }
};

// Message table
struct [[eosio::table("message"), eosio::contract("talk")]] message {
    uint64_t    id       = {}; // Non-0
    uint64_t    reply_to = {}; // Non-0 if this is a reply
    eosio::name user     = {};
    std::string content  = {};
    reaction_tally stats = {};

    uint64_t primary_key() const { return id; }
    uint64_t get_reply_to() const { return reply_to; }
};

using message_table = eosio::multi_index<
    "message"_n, message, eosio::indexed_by<"by.reply.to"_n, eosio::const_mem_fun<message, uint64_t, &message::get_reply_to>>>;

struct [[eosio::table("contributors"), eosio::contract("talk")]] contributors {
    eosio::name name = {};
    std::map<uint64_t, reaction_type_type> reactions;

    uint64_t primary_key() const { return name.value; }
};

using contributor_table = eosio::multi_index<"contributors"_n, contributors>;

// The contract
class talk : eosio::contract {

    /* Record a new reaction for a talk
    *
    * @param talk      The talk to update
    * @param reaction  The reaction to update
    */
    void record_reaction(uint64_t talk, reaction_type_type r)  {
        message_table table(get_self(), 0);

        auto message = table.find(talk);

        if(message != table.end()) {
            table.modify(message, _self, [&](auto &row) {
                row.stats += r;
            });
        }
    }

    /* Change the reaction to a talk
    *
    * @param talk  The talk to update
    * @param from  The original reaction
    * @param to    The new reaction
    */
    void change_reaction(uint64_t talk, reaction_type_type from, reaction_type_type to) {
        message_table table(get_self(), 0);

        auto message = table.find(talk);
        eosio::check(message != table.end(), "Referenced talk does not exist");

        table.modify(message, _self, [&](auto &row) {
            row.stats -= from;
            row.stats += to;
        });
    }

    /* Process a user's reaction
    *
    * @param user      The user posting the reaction
    * @param reply_to  The message the user is reacting to
    * @param reaction  The user's reaction to the message
    */
    void process_reaction(eosio::name user, uint64_t reply_to, reaction_tally::type reaction) {
        // check user
        require_auth(user);

        // check reply-to
        message_table table(get_self(), 0);

        reaction_type_type r = (reaction_type_type)reaction;

        auto message = table.find(reply_to);
        eosio::check(message != table.end(), "No such message to react to");

        // if first time contributor
        contributor_table current_reactions(get_self(), 0);
        auto current = current_reactions.find(user.value);
        if(current == current_reactions.end()) {
            current_reactions.emplace(_self, [&](auto &row) {
                row.name = user;
                row.reactions[reply_to] = r;
                record_reaction(reply_to, r);
            });
            return;
        } else {
            // if existing contributor
            current_reactions.modify(current, _self, [&](auto &row) {
                auto old = row.reactions.find(reply_to);

                // if first time contributing to this talk
                if(old == row.reactions.end()) {
                    record_reaction(reply_to, r);
                    row.reactions[reply_to] = r;
                }
                // if changed mind about reaction
                else if(old->second != r) {
                    change_reaction(reply_to, old->second, r);
                    old->second = r;
                }
            });
        }
    }
  public:
    // Use contract's constructor
    using contract::contract;

    // Post a message
    [[eosio::action]] void post(uint64_t id, uint64_t reply_to, eosio::name user, const std::string& content) {
        message_table table{get_self(), 0};

        // Check user
        require_auth(user);

        // Check reply_to exists
        if (reply_to)
            table.get(reply_to);

        // Create an ID if user didn't specify one
        eosio::check(id < 1'000'000'000ull, "user-specified id is too big");
        if (!id)
            id = std::max(table.available_primary_key(), 1'000'000'000ull);

        // Record the message
        table.emplace(get_self(), [&](auto& message) {
            message.id       = id;
            message.reply_to = reply_to;
            message.user     = user;
            message.content  = content;
            message.stats    = reaction_tally();
        });
    }

    [[eosio::action]]
    void thumbsup(eosio::name user, uint64_t reply_to) {
        process_reaction(user, reply_to, reaction_tally::type::thumbs_up);
    }

    [[eosio::action]]
    void thumbsdown(eosio::name user, uint64_t reply_to) {
        process_reaction(user, reply_to, reaction_tally::type::thumbs_down);
    }
};
