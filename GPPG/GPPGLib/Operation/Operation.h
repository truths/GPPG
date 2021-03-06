/*
 *  Operation.h
 *  GPPG
 *
 *  Created by Troy Ruths on 5/11/12.
 *  Copyright 2012 Rice University. All rights reserved.
 *
 */

#ifndef CORE_OPERATION_
#define CORE_OPERATION_


#include "Base/Genotype.h"
#include "Base/GenotypeFactory.h"
#include "Base/Mutator.h"
#include "Base/Recombinator.h"

#include <set>
#include <iostream>

namespace GPPG {

	
	/** IOperation adds additional functions to the IGenotype interface.  In doing so,
	 * Genotypes may be represented as a cascade of Operations, which is much more memory efficient.
	 */
	class IOperation: public IGenotype  {
	public:
		virtual ~IOperation() {}
		
		/** Retrieves the Genotype generated by this operation.  
		 * It may be the same object.
		 */
		virtual const IGenotype& genotype() const = 0;
		
		/** Set this Operation to be compressed or uncompressed.
		 * Uncompressing occurs during this function call --- so don't use it unless you have to!
		 */
		virtual void setCompressed(bool compress) = 0;
		virtual bool isCompressed() const = 0;
		
		
		/** Returns the cost of applying this operation.
		 * The cost is provided in the construction of the operation and should take into account
		 * the complexity of the operation and the amount of CPU-time required to apply it.
		 */
		virtual int cost() const = 0;
		
		/** Returns the data footprint of this operation (plus cache)
		 *
		 */
		virtual int dataSize() const = 0;
		
		/** Returns the number of child operations.
		 * The children of an Operation are implicitly acquired which a subsequent Operation object uses this Operation as its parent.
		 */ 
		virtual int numChildren() const = 0;
		
		virtual const std::set<IOperation*>& children() const = 0;
		
		/** Retrieves the first or second parent.
		 *  @param i - the parent to retrieve (0 or 1)
		 */ 
		virtual IOperation* parent(int i) = 0;
		virtual const IOperation* parent(int i) const = 0;
		
		/** The number of parents for this operation.
		 */
		virtual int numParents() const = 0;
		
		/** Requests cache
		*/
		virtual int requests() const = 0;
		virtual void clearRequests() = 0;
		virtual void clearDescendentRequests() = 0;
		virtual void setRequests(int i) = 0;
		virtual void incrRequests(int i) = 0;
		virtual void decrRequests(int i) = 0;
		virtual void touch() = 0;
		
		virtual std::string toString() const = 0;
	};
	
	class BaseOperation : public IOperation {
	public:
		BaseOperation(int cost);
		~BaseOperation();
		
		int key() const;
		void setKey(int k);
		
		void configure();
		double frequency() const;
		void setFrequency(double freq);
		
		double total() const;
		void setTotal(double total);
		
		bool isActive() const;
		
		int index() const;
		void setIndex(int i);
		
		int state() const;
		void setState(int i);
		
		int order() const;
		void setOrder(int i);
		
		double fitness() const;
		void setFitness(double f);
		 
		int requests() const;
		void clearRequests();
		void clearDescendentRequests();
		void setRequests(int i);
		void incrRequests(int i);
		void decrRequests(int i);
		void touch();
		
		void setCompressed( bool c );
		
		const char* exportFormat();
		/**
		 * The cost of applying the operation
		 */
		int cost() const;
		void setCost(int v);
		
		std::string toString() const;
		
	protected:
		double _freq, _total, _fitness;
		int _index, _order, _key, _state, _requests;
		unsigned short _touch;
		double _load, _loadFreq, _loadCost;
		int _cost;
	};
	
	
	template <typename T, class P> class Operation : public BaseOperation, public P {
			
	public:
		Operation<T,P>(int cost): BaseOperation(cost), _data(0) { innerConstructor( 0, 0); }
		Operation<T,P>(int cost, Operation<T,P> &parent) : BaseOperation(cost), _data(0) { innerConstructor( &parent, 0); }
		Operation<T,P>(int cost, Operation<T,P> &parent1, Operation<T,P> &parent2): BaseOperation(cost), _data(0) { innerConstructor( &parent1, &parent2); }
		
		~Operation<T,P>() {
			if (_data) {
				delete _data;
			}
			
			// Remove from parents
			if(_parent1) _parent1->removeChild( this );
			if(_parent2) _parent2->removeChild( this );
			
			
			// Delete children
			typename std::set< Operation<T,P> *>::iterator it=_children.begin(); 
			while(it!=_children.end()) {
				Operation<T,P>* op = *it;
				it++;
				delete op;
			}

		}
		
		
		Operation<T,P> const* parent(int i) const {
			if( i<0 || i>=numParents()) { throw "Incorrect index"; }
			
			switch( i ){
				case 0: return _parent1; 
				case 1: return _parent2;
			}
			
			throw "Index is too high";
		}
		
		Operation<T,P>* parent(int i) {
			if( i<0 || i>=numParents()) { throw "Incorrect index"; }
			
			switch( i ){
				case 0: return _parent1; 
				case 1: return _parent2;
			}
			
			throw "Index is too high";
		}
		
		virtual int numParents() const {
			if (_parent2 != 0) {
				return 2;
			} else if (_parent1 != 0) {
				return 1;
			}
			return 0;
		}
		
		const IGenotype& genotype() const { return *this; }
		
		/** Returns a pointer to the data produced by this Operation.
		 * Returns the result of this operation. If the cache is full, then it will be returned quickly; 
		 * otherwise, the operation will be evaluated, along with its parents.
		 */
		/* NOTE: I commented this out b/c it is inconsistent.
		T* data() const {
			if (isCompressed()) {
				return ;
			}
			return Genotype<T>::data();
		}
		*/
		
		/**
		 Returns the number of children
		 */
		int numChildren() const {
			return _children.size();
		}
		
		const std::set<IOperation* >& children() const {
			return (const std::set<IOperation*>&)_children;
		}
		
		// Manage compression
		void setCompressed(bool compress) {
			BaseOperation::setCompressed(compress);
			if (compress && !isCompressed()) {
				setData(0);
			} else if (!compress && isCompressed()) {
				// Fill the cache
				_data = evaluate();
			}
		}
		
		/** Returns the data cache.
		 * It may return a NULL pointer.
		 */
		virtual T* data() const { return _data; }
		
		/** Returns the data cache.
		 * This function will NOT return a NULL pointer.  However, it may cause an evaluation of the operation.  
		 * If an evaluation occurs, then \param isCopy is set to 1 and the data should be deleted by the caller.
		 */
		T* data(int& isCopy) {
			if (isCompressed()) {
				isCopy = 1;
				return evaluate();
			} 
			isCopy = 0;
			return data();
		}
		
		void setData(T* d) {
			if (_data) {
				delete _data;
			}
			_data = d;
		}
		
		bool isCompressed() const { return _data == 0; }
		
		// Data Size
		int dataSize() const { return 1; }
		
	
		/** Returns a no-strings attached evaluation of this Operation.
		 * Consuming code must delete the result when finished with it.
		 */
		virtual T* evaluate() {
			incrRequests(1);
			if (isCompressed()) {
				return NULL;
			}
			//return data()->copy();	
			return _data->copy();
		}
	
		
		
	private:
		Operation<T,P>(Operation<T,P> const& op) {}
		Operation<T,P>& operator=(Operation<T,P> const& op) {}
		
		void innerConstructor(Operation<T,P>* parent1, Operation<T,P>* parent2) {
			_parent1 = parent1;
			_parent2 = parent2;
			
			if (_parent1 != 0) {
				_parent1->addChild( this );
			}
			if (_parent2 != 0) {
				_parent2->addChild( this );
			}
			
			_load = 0;
		}
		
		void addChild(Operation<T,P>* op) {
			_children.insert(op);
		}
		
		void removeChild(Operation<T,P>* op) {
			_children.erase( op );
		}
		
		std::set< Operation<T,P> *> _children;
		
		T* _data;
		
		Operation<T,P> *_parent1, *_parent2;
		
		
	};
	
	template <typename T, class P> class OperationRoot : public Operation<T,P> {
	public:
		OperationRoot<T,P>(T* data) : Operation<T,P>(1) { setData(data); }
		
		void setCompressed(bool c) {
			BaseOperation::setCompressed( false );
		}
		
		bool isCompressed() { return false; }
		
		std::string toString() const { return "OperationRoot"; }
	};
	
	template <typename T, class P> class OperationFactory : public GenotypeFactory<T> {
	public:
		OperationRoot<T,P>* random() const { return new OperationRoot<T,P>( randomData() ); }
		
		virtual T* randomData() const = 0;
	};
	
	template <typename T>
	class OperationMutator : public IMutator {
	public:
		OperationMutator(int cost): _cost(cost) {}
		
		IGenotype* mutate(IGenotype& geno) const {
			return mutate( (T&)geno );
		}
		
		int numMutants(IGenotype& geno, long N, double f) const {
			return numMutants( (T&)geno, N, f);
		}
		
		virtual int numMutants(T& g, long N, double f) const = 0;
		
		virtual T* mutate(T& op) const = 0;
		
		int cost() const { return _cost; }
		
	private:
		int _cost;
	};
	
	template <typename T>
	class OperationRecombinator : public IRecombinator {
	public:
		OperationRecombinator(int cost) : _cost(cost) {}
		
		IGenotype* recombine(IGenotype& geno1, IGenotype& geno2) const {
			return recombine( (T&)geno1, (T&)geno2 );
		}
		
		int numMutants(IGenotype& geno1, IGenotype& geno2, long N) const {
			return numMutants( (T&)geno1, (T&)geno2, N);
		}
		
		virtual int numMutants(T& g, T& g2, long N) const = 0;
		
		virtual T* recombine(T& op, T& op2) const = 0;
		
		int cost() const {return _cost; }
		
	private:
		int _cost;
	};
}

std::ostream& operator<<(std::ostream& output, const GPPG::IOperation& op);

template <typename T, class P>
std::ostream& operator<<(std::ostream& output, const GPPG::Operation<T,P>& op) {
	// Print the Operation
	output << "Operation (" << op.isCompressed() << "): \n";
	for (int i=0; i<op.numParents(); i++) {
		output << "\tParent[" << i << "]: " << op.parent(i)->toString() << std::endl;
	}
	
	output << "\tContent: " << op.toString() << std::endl;
	T* data = op.evaluate();
	output << "\tData: " << *data << std::endl;
	delete data;
	return output;
}

#endif
