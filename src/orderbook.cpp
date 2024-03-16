/**
 * @file order.cpp
 * @brief This file contains the implementation of the Orderbook class.
 */

#include <iostream>
#include <vector>
#include <chrono>
#include <stdlib.h> 
#include <map>
#include <thread>
#include <iomanip>
#include <memory>

#include "../include/order.hpp"
#include "../include/orderbook.hpp"

void Orderbook::add_order(int qty, double price, BookSide side){
	if(side == bid){
		bids[price].push_back(make_unique<Order>(qty, price, side));
	}else{
		asks[price].push_back(make_unique<Order>(qty, price, side));
	}
}

Orderbook::Orderbook(){
	// seed RNG
	srand (time(NULL)); 

	// Add some dummy orders
	for (int i=0; i<10; i++){ 
		double random_price = 90.0 + (rand() % 1001) / 100.0;
		int random_qty = rand() % 100 + 1; 
		int random_qty2 = rand() % 100 + 1;
		
		add_order(random_qty, random_price, BookSide::bid);
		this_thread::sleep_for(chrono::milliseconds(1)); // Sleep for a millisecond so the orders have different timestamps
		add_order(random_qty2, random_price, BookSide::bid);
	}
	for (int i=0; i<10; i++){
		double random_price = 100.0 + (rand() % 1001) / 100.0;
		int random_qty = rand() % 100 + 1; 
		int random_qty2 = rand() % 100 + 1; 			
		
		add_order(random_qty, random_price, BookSide::ask);
		this_thread::sleep_for(chrono::milliseconds(1)); // Sleep for a millisecond so the orders have different timestamps
		add_order(random_qty2, random_price, BookSide::ask);
	}
}

template<typename T>
void Orderbook::clean_leg(map<double, vector<unique_ptr<Order>>, T>& map){
	for (auto it = map.begin(); it != map.end(); ) {
		if (it->second.empty()) { // Check if the vector associated with the current key is empty
			it = map.erase(it);  // Erase the key and update the iterator to the next element
		} else {
			++it; // Move to the next element
		}
	}
}

void Orderbook::remove_empty_keys(){
	// Remove all empty keys from map
	clean_leg(bids);
	clean_leg(asks);
}

// Return avg fill price and qty
tuple<int,double> Orderbook::execute_order(OrderType type, int order_quantity, Side side, double price){
	// Analytics 
	int units_transacted = 0;
	double total_value = 0;

	auto process = [&] (auto& offers, Side side) {
		for(auto rit = offers.rbegin(); rit != offers.rend();) {
			auto& pair = *rit; // Dereference iterator to get the key-value pair
			double price_level = pair.first;
			
			auto& quotes = pair.second; // Order vector

			// Sort quotes in ascending order by timestamp
			sort(quotes.begin(), quotes.end(), [](const unique_ptr<Order>& a, const unique_ptr<Order>& b) {
				return a->get_timestamp() < b->get_timestamp();
			});

			// Ensure agreeable price for limit order
			bool can_transact = true; 
			if (type == limit){
				if (side == buy && price_level > price){
					can_transact = false;
				}else if(side == sell && price_level < price){
					can_transact = false;
				}
			}

			// lift/hit as many levels as needed until qty filled
			auto it = quotes.begin();
			while(it != quotes.end() && order_quantity > 0 && can_transact){
				int quote_qty = (*it)->get_quantity();
				uint64_t timestamp = (*it)->get_timestamp();
				if(quote_qty > order_quantity){
					units_transacted += order_quantity;
					total_value += order_quantity * pair.first;

					// Fill part of this order and break
					(*it)->set_quantity(quote_qty-order_quantity);
					quote_qty -= order_quantity;
					order_quantity = 0;
					break;
				}else{
					units_transacted += quote_qty;
					total_value += quote_qty * price_level;
					
					// delete order from book and on
					order_quantity -= quote_qty;
					it = quotes.erase(it);
				}
			}
			rit++;
		}
		remove_empty_keys();
		return make_tuple(units_transacted, total_value);
	};
	
	// market order
	if (type == market) {

		return (side == sell) ? process(bids, Side::sell) : process(asks, Side::buy);

	} else if(type == limit) {
		// Analytics 
		int units_transacted = 0;
		double total_value = 0;

		if (side==buy){
			if (best_quote(BookSide::ask) <= price){
				// Can at least partially fill
				tuple<int, double> fill = process(asks, Side::buy);
				// Add remaining order to book
				add_order(order_quantity, price, BookSide::bid);
				return fill;
			}else{
				// No fill possible, put on book
				add_order(order_quantity, price, BookSide::bid);
				return make_tuple(units_transacted, total_value);
			}

		}else{
			if (best_quote(BookSide::bid) >= price){
				// Can at least partially fill
				tuple<int, double> fill = process(bids, Side::sell);
				// Add remaining order to book
				add_order(order_quantity, price, BookSide::ask);
				return fill;
			}else{
				// No fill possible, put on book
				add_order(order_quantity, price, BookSide::ask);
				return make_tuple(units_transacted, total_value);
			}
		}
	}else{
		throw std::runtime_error("Invalid order type encountered");
	}
}

double Orderbook::best_quote(BookSide side){
	if (side == bid){
		return std::prev(bids.end())->first;
	}else if (side == ask){
		return std::prev(asks.end())->first;
	} else {
		return 0.0;
	}
}

template<typename T>
void Orderbook::print_leg(map<double, vector<unique_ptr<Order>>, T>& hashmap, BookSide side){
	if (side == ask){
		for(auto& pair : hashmap){ // Price level
			// Count size at a price level
			int size_sum = 0;
			for (auto& order : pair.second){ // Order level
				size_sum += order->get_quantity();
			}

			string color = "31";
			cout << "\t\033[1;" << color << "m" << "$" << setw(6) << fixed << setprecision(2) << pair.first << setw(5) << size_sum << "\033[0m ";

			// Make bars to visualize size
			for (int i = 0; i < size_sum/10; i++){
				cout << "█";
			}
			cout << endl;
		}
	}else if (side == bid){
		for(auto pair = hashmap.rbegin(); pair != hashmap.rend(); ++pair) { // Price level
			int size_sum = 0;
			for (auto& order : pair->second){ // Order level
				size_sum += order->get_quantity();
			}

			string color = "32";
			cout << "\t\033[1;" << color << "m" << "$" << setw(6) << fixed << setprecision(2) << pair->first << setw(5) << size_sum << "\033[0m ";

			// Make bars to visualize size
			for (int i = 0; i < size_sum/10; i++){
				cout << "█";
			}
			cout << endl;
		}
	}
	
}

void Orderbook::print(){
	cout << "========== Orderbook =========" << endl;
	print_leg(asks, BookSide::ask);

	// print bid-ask spread
	double best_ask = best_quote(BookSide::ask);
	double best_bid = best_quote(BookSide::bid);
	cout  << "\n\033[1;33m" << "======  " << 10000 * (best_ask-best_bid)/best_bid << "bps  ======\033[0m\n\n";

	print_leg(bids, BookSide::bid);
	cout << "==============================\n\n\n";
}