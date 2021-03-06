// CURRENTLY DOES NOT COMPILE
// WORK IN PROGRESS
// # 2018 Travis Moore, Kedar Iyer, Sam Kazemian
// # MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM
// # MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM
// # MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM
// # MMMMMMMMMMMMMMMMMmdhhydNMMMMMMMMMMMMMMMMMMMMMMMMMM
// # MMMMMMMMMMMMMNdy    hMMMMMMNdhhmMMMddMMMMMMMMMMMMM
// # MMMMMMMMMMMmh      hMMMMMMh     yMMM  hNMMMMMMMMMM
// # MMMMMMMMMNy       yMMMMMMh       MMMh   hNMMMMMMMM
// # MMMMMMMMd         dMMMMMM       hMMMh     NMMMMMMM
// # MMMMMMMd          dMMMMMN      hMMMm       mMMMMMM
// # MMMMMMm           yMMMMMM      hmmh         NMMMMM
// # MMMMMMy            hMMMMMm                  hMMMMM
// # MMMMMN             hNMMMMMmy                 MMMMM
// # MMMMMm          ymMMMMMMMMmd                 MMMMM
// # MMMMMm         dMMMMMMMMd                    MMMMM
// # MMMMMMy       mMMMMMMMm                     hMMMMM
// # MMMMMMm      dMMMMMMMm                      NMMMMM
// # MMMMMMMd     NMMMMMMM                      mMMMMMM
// # MMMMMMMMd    NMMMMMMN                     mMMMMMMM
// # MMMMMMMMMNy  mMMMMMMM                   hNMMMMMMMM
// # MMMMMMMMMMMmyyNMMMMMMm         hmh    hNMMMMMMMMMM
// # MMMMMMMMMMMMMNmNMMMMMMMNmdddmNNd   ydNMMMMMMMMMMMM
// # MMMMMMMMMMMMMMMMMMMMMMMMMMMNdhyhdmMMMMMMMMMMMMMMMM
// # MMMMMMMMMMMMMMMMMMMMMMMMMMNNMMMMMMMMMMMMMMMMMMMMMM
// # MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM
// # MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM


#include "eparticle.hpp"

bool eparticle::is_new_user (const account_name& _thisaccount){
    return true;
}


void eparticle::propose( account_name proposer, ipfshash_t& proposed_article_hash, ipfshash_t& old_article_hash ) {

    // Verify minimum brainpower is met
    brainpower_table braintable(_self, _self);

    auto brain_it = braintable.find(proposer);
    eosio_assert(brain_it->power > EDIT_PROPOSE_BRAINPOWER, "Not enough brainpower to edit");

    // store the proposal
    edit_proposals_table proptable( _self, proposer );
    proptable.emplace( _self, [&]( auto& a ) {
        a.id = 1;
        a.proposed_article_hash = proposed_article_hash;
        a.old_article_hash = old_article_hash;
        a.timestamp = now();
        a.status = ProposalStatus::pending;
    });

    // TODO: Replace with proper ID instead of 1
    eparticle::placevote( proposer, 1, true, EDIT_PROPOSE_BRAINPOWER );
}


void eparticle::placevote ( account_name voter, uint64_t proposal_id, bool approve, uint64_t amount ) {

    // Check if article exists
    edit_proposals_table proptable( _self, voter );
    auto prop_it = proptable.find( proposal_id );
    eosio_assert( prop_it != proptable.end(), "proposal not found" );

    // Verify voting is still in progress
    eosio_assert( now() < prop_it->timestamp + DEFAULT_VOTING_TIME, "voting period is over");

    // Consume brainpower
    brainpower_table braintable(_self, _self);
    auto brain_it = braintable.find(voter);
    eosio_assert(brain_it->power >= amount, "Not enough brainpower");
    braintable.modify( brain_it, 0, [&]( auto& b ) {
        b.sub(amount);
    });

    // Store vote in DB
    votes_table votetbl( _self, voter );
    votetbl.emplace( voter, [&]( auto& a ) {
         // TODO: incrementing IDs
         a.id = 1;
         a.proposal_id = proposal_id;
         a.approve = approve;
         a.amount = amount;
         a.voter = voter;
         a.timestamp = now();
    });

}

void eparticle::countvotes( account_name proposer, uint64_t proposal_id ) {
    // Need a for loop that tallys the yes's and no's for a given proposal_id and returns a string
    // This is needed for the voting page on the .org site, and is meant as a helper function
    // Example: {"yes": 93827, "no": 3948}
}

void eparticle::getvotes( account_name proposer, uint64_t proposal_id ) {
    // Needs to return, in JSON format, a list of all vote structs for a given proposal_id
    // Look into JSON serialization for that struct
}

void eparticle::getvotingperiod( account_name proposer, uint64_t proposal_id ) {
    // Needs to return, in JSON format, the start and the end time of voting for a given proposal in epoch seconds
    // Example: {"start": 1526069969, "end": 1526156621}
}

void eparticle::finalize( account_name from, uint64_t proposal_id ) {

    // Verify proposal exists
    edit_proposals_table proptable( _self, _self );
    auto prop_it = proptable.find( proposal_id );
    eosio_assert( prop_it != proptable.end(), "proposal not found" );

    // Verify voting period is complete
    eosio_assert( now() > prop_it->timestamp + DEFAULT_VOTING_TIME, "voting period is over");

    // Retrieve votes from DB
    votes_table votetable(_self, _self);
    auto propidx = votetable.get_index<N(byproposal)>();
    auto vote_it = propidx.find( std::forward<uint64_t>(proposal_id) );
    eosio_assert( vote_it != propidx.end(), "no votes found for proposal");


    // Tally votes
    uint64_t for_votes = 0;
    uint64_t against_votes = 0;
    while(vote_it->proposal_id == proposal_id) {
        if (vote_it->approve)
            for_votes += vote_it->amount;
        else
            against_votes += vote_it->amount;
        vote_it++;
    }

    // Mark proposal as accepted or rejected. Ties are rejected
    proptable.modify( prop_it, 0, [&]( auto& a ) {
        if (for_votes > against_votes)
            a.status =  ProposalStatus::accepted;
        else
            a.status =  ProposalStatus::rejected;
    });

    // Determine slashing conditions
    // floating point is inexact, so I'm using integer arithmetic for slashing percentages
    vote_it = propidx.find( std::forward<uint64_t>(proposal_id) );
    bool approved = (for_votes > against_votes);
    uint64_t slash_percent;
    if (approved)
        slash_percent = for_votes - against_votes;
    else
        slash_percent = against_votes - for_votes;

    // Slash losers
    while(vote_it->proposal_id == proposal_id) {
        if (vote_it->approve != approved) {
            uint64_t slash_amount = vote_it->amount;
            brainpower_table braintable(_self, _self);
            auto brain_it = braintable.find(vote_it->voter);
            braintable.modify( brain_it, 0, [&]( auto& b ) {
                for (auto stake_it = b.stakes.begin(); slash_amount > 0; stake_it++) {
                    // STAKING_DURATION is a known constant and slash_percent has a max value of 100, so this will not overflow
                    stake_it->duration += STAKING_DURATION * slash_percent / 100;
                    slash_amount -= stake_it->amount;
                }
            });
        }
        vote_it++;
    }

    // TODO: Reward the voters

    // Add article to database
    wikis_table wikitbl( _self, _self );
    wikitbl.emplace( _self,  [&]( auto& a ) {
        // TODO: incrementing ID
        a.hash = prop_it->proposed_article_hash;
        a.parent_hash = prop_it->old_article_hash;
    });
}

void eparticle::brainme( account_name from, uint64_t amount ) {
    brainpower_table braintable(_self, _self);
    auto brain_it = braintable.find(from);

    // TODO: transfer IQ to contract

    // boost brainpower
    braintable.modify( brain_it, 0, [&]( auto& b ) {
        b.add(amount);

        stake s;
        s.amount = amount;
        s.timestamp = now();
        s.duration = STAKING_DURATION;
        b.stakes.push_back(s);
    });
}

EOSIO_ABI( eparticle, (brainme)(finalize)(propose)(placevote) )
