/*
 * This file is protected by Copyright. Please refer to the COPYRIGHT file
 * distributed with this source distribution.
 * 
 * This file is part of GNUHAWK.
 * 
 * GNUHAWK is free software: you can redistribute it and/or modify is under the 
 * terms of the GNU General Public License as published by the Free Software 
 * Foundation, either version 3 of the License, or (at your option) any later 
 * version.
 * 
 * GNUHAWK is distributed in the hope that it will be useful, but WITHOUT ANY 
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS 
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more 
 * details.
 * 
 * You should have received a copy of the GNU General Public License along with 
 * this program.  If not, see http://www.gnu.org/licenses/.
 */

#include "null_sink_i_base.h"

/*******************************************************************************************

    AUTO-GENERATED CODE. DO NOT MODIFY

    The following class functions are for the base class for the component class. To
    customize any of these functions, do not modify them here. Instead, overload them
    on the child class

******************************************************************************************/

null_sink_i_base::null_sink_i_base(const char *uuid, const char *label) :
    GnuHawkBlock(uuid, label),
    serviceThread(0),
    noutput_items(0),
    _maintainTimeStamp(false),
    _throttle(false)
{
    construct();
}

void null_sink_i_base::construct()
{
    Resource_impl::_started = false;
    loadProperties();
    serviceThread = 0;
    inputPortOrder.resize(0);;

    PortableServer::ObjectId_var oid;
    long_in = new bulkio::InLongPort("long_in");
    long_in->setNewStreamListener(this, &null_sink_i_base::long_in_newStreamCallback);
    oid = ossie::corba::RootPOA()->activate_object(long_in);

    registerInPort(long_in);
    inputPortOrder.push_back("long_in");

}

/*******************************************************************************************
    Framework-level functions
    These functions are generally called by the framework to perform housekeeping.
*******************************************************************************************/
void null_sink_i_base::initialize() throw (CF::LifeCycle::InitializeError, CORBA::SystemException)
{
}

void null_sink_i_base::start() throw (CORBA::SystemException, CF::Resource::StartError)
{
    boost::mutex::scoped_lock lock(serviceThreadLock);
    if (serviceThread == 0) {
        long_in->unblock();
        serviceThread = new ProcessThread<null_sink_i_base>(this, 0.1);
        serviceThread->start();
    }
    
    if (!Resource_impl::started()) {
    	Resource_impl::start();
    }
}

void null_sink_i_base::stop() throw (CORBA::SystemException, CF::Resource::StopError)
{
    if ( long_in ) long_in->block();
    {
        boost::mutex::scoped_lock lock(_sriMutex);
        _sriQueue.clear();
    }

    // release the child thread (if it exists)
    if (serviceThread != 0) {
      {
        boost::mutex::scoped_lock lock(serviceThreadLock);
        LOG_TRACE( null_sink_i_base, "Stopping Service Function" );
        serviceThread->stop();
      }

      if ( !serviceThread->release()) {
         throw CF::Resource::StopError(CF::CF_NOTSET, "Processing thread did not die");
      }

      boost::mutex::scoped_lock lock(serviceThreadLock);
      if ( serviceThread ) {
        delete serviceThread;
      }
    }
    serviceThread = 0;

    if (Resource_impl::started()) {
        Resource_impl::stop();
    }
    
    LOG_TRACE( null_sink_i_base, "COMPLETED STOP REQUEST" );
}

CORBA::Object_ptr null_sink_i_base::getPort(const char* _id) throw (CORBA::SystemException, CF::PortSupplier::UnknownPort)
{

    std::map<std::string, Port_Provides_base_impl *>::iterator p_in = inPorts.find(std::string(_id));
    if (p_in != inPorts.end()) {
        if (!strcmp(_id,"long_in")) {
            bulkio::InLongPort *ptr = dynamic_cast<bulkio::InLongPort *>(p_in->second);
            if (ptr) {
                return ptr->_this();
            }
        }
    }

    std::map<std::string, CF::Port_var>::iterator p_out = outPorts_var.find(std::string(_id));
    if (p_out != outPorts_var.end()) {
        return CF::Port::_duplicate(p_out->second);
    }

    throw (CF::PortSupplier::UnknownPort());
}

void null_sink_i_base::releaseObject() throw (CORBA::SystemException, CF::LifeCycle::ReleaseError)
{
    // This function clears the component running condition so main shuts down everything
    try {
        stop();
    } catch (CF::Resource::StopError& ex) {
        // TODO - this should probably be logged instead of ignored
    }

    // deactivate ports
    releaseInPorts();
    releaseOutPorts();

    delete(long_in);

    Resource_impl::releaseObject();
}

void null_sink_i_base::loadProperties()
{
    addProperty(sizeof_stream_item,
                1,
                "sizeof_stream_item",
                "sizeof_stream_item",
                "readwrite",
                "",
                "external",
                "configure");

}

//
//  Allow for logging 
// 
PREPARE_LOGGING(null_sink_i_base);

inline static unsigned int
round_up (unsigned int n, unsigned int multiple)
{
  return ((n + multiple - 1) / multiple) * multiple;
}

inline static unsigned int
round_down (unsigned int n, unsigned int multiple)
{
  return (n / multiple) * multiple;
}

void null_sink_i_base::setupIOMappings( )
{
    int ninput_streams = 0;
    int noutput_streams = 0;
    std::vector<std::string>::iterator pname;
    std::string sid("");
    int inMode=RealMode;

    if ( !validGRBlock() ) return;
    ninput_streams  = gr_sptr->get_max_input_streams();
    gr_io_signature_sptr g_isig = gr_sptr->input_signature();

    LOG_DEBUG( null_sink_i_base, "GNUHAWK IO MAPPINGS IN/OUT " << ninput_streams << "/" << noutput_streams );

    //
    // Someone reset the GR Block so we need to clean up old mappings if they exists
    // we need to reset the io signatures and check the vlens
    //
    if ( _istreams.size() > 0 ) {

        LOG_DEBUG( null_sink_i_base, "RESET INPUT SIGNATURE SIZE:" << _istreams.size() );
        IStreamList::iterator istream;
        for ( int idx=0 ; istream != _istreams.end(); idx++, istream++ ) {
            // re-add existing stream definitons
            LOG_DEBUG(  null_sink_i_base, "ADD READ INDEX TO GNU RADIO BLOCK");
            if ( ninput_streams == -1 ) gr_sptr->add_read_index();

            // setup io signature
            istream->associate( gr_sptr );
        }


        return;
    }


   //
   // Setup mapping of RH port to GNU RADIO Block input streams
   // For version 1,  we are ignoring the GNU Radio input stream -1 case that allows multiple data 
   // streams over a single connection.  We are mapping a single RH Port to a single GNU Radio stream.
   // Stream Identifiers will  be pass along as they are received
   //
    LOG_TRACE( null_sink_i_base, "setupIOMappings INPUT PORTS: " << inPorts.size() );
    pname = inputPortOrder.begin();
    for( int i=0; pname != inputPortOrder.end(); pname++ ) {

        // grab ports based on their order in the scd.xml file
        RH_ProvidesPortMap::iterator p_in = inPorts.find(*pname);
        if ( p_in != inPorts.end() ) {
            bulkio::InLongPort *port = dynamic_cast< bulkio::InLongPort * >(p_in->second);
            int mode = inMode;
            sid = "";

            // need to add read index to GNU Radio Block for processing streams when max_input == -1
            if ( ninput_streams == -1 ) gr_sptr->add_read_index();

            // check if we received SRI during setup
            BULKIO::StreamSRISequence_var sris = port->activeSRIs();
            if (  sris->length() > 0 ) {
                BULKIO::StreamSRI sri = sris[sris->length()-1];
                mode = sri.mode;
            }
            std::vector<int> in;
            io_mapping.push_back( in );
            _istreams.push_back( gr_istream< bulkio::InLongPort > ( port, gr_sptr, i, mode, sid ));
            LOG_DEBUG( null_sink_i_base, "ADDING INPUT MAP IDX:" << i << " SID:" << sid );
            // increment port counter
            i++;
        }
    } 

}

void null_sink_i_base::long_in_newStreamCallback( BULKIO::StreamSRI &sri )
{
  LOG_TRACE( null_sink_i_base, "START NotifySRI  port:stream " << long_in->getName() << "/" << sri.streamID);

  boost::mutex::scoped_lock lock(_sriMutex);
  _sriQueue.push_back( std::make_pair( long_in, sri ) );

  LOG_TRACE( null_sink_i_base, "END  NotifySRI  QUEUE " << _sriQueue.size() << " port:stream " << long_in->getName() << "/" << sri.streamID); 
}


void null_sink_i_base::processStreamIdChanges()
{
    boost::mutex::scoped_lock lock(_sriMutex);

    LOG_TRACE( null_sink_i_base, "processStreamIDChanges QUEUE: " << _sriQueue.size()  );
    if (  _sriQueue.size() == 0 ) return;
    std::string sid("");

    if ( validGRBlock() ) {

        IStreamList::iterator istream;
        int idx=0;
        std::string sid("");
        int mode=0;
        SRIQueue::iterator item = _sriQueue.begin();

        for ( ; item != _sriQueue.end(); item++ ) {
            idx = 0;
            sid = "";
            mode= item->second.mode;
            sid = item->second.streamID;
            istream = _istreams.begin();
            for ( ; istream != _istreams.end(); idx++, istream++ ) {

                if ( istream->port == item->first ) {
                    LOG_DEBUG( null_sink_i_base,  "  SETTING IN_STREAM ID/STREAM_ID :" << idx << "/" << sid  );
                    istream->sri(true);
                    istream->spe(mode);

                }
            }
        }

        _sriQueue.clear();

    } else {
        LOG_WARN( null_sink_i_base, " NEW STREAM ID, NO GNU RADIO BLOCK DEFINED, SRI QUEUE SIZE:" << _sriQueue.size() );
    }

}

null_sink_i_base::TimeDuration null_sink_i_base::getTargetDuration()
{
    TimeDuration  t_drate;;
    uint64_t samps=0;
    double   xdelta=1.0;
    double   trate=1.0;

    trate = samps*xdelta;
    uint64_t sec = (uint64_t)trunc(trate);
    uint64_t usec = (uint64_t)((trate-sec)*1e6);
    t_drate = boost::posix_time::seconds(sec) + 
            boost::posix_time::microseconds(usec);
    LOG_TRACE( null_sink_i_base, " SEC/USEC " << sec << "/"  << usec << "\n"  <<
              " target_duration " << t_drate );
    return t_drate;
}

null_sink_i_base::TimeDuration null_sink_i_base::calcThrottle( TimeMark &start_time,
                                             TimeMark &end_time )
{
    TimeDuration delta;
    TimeDuration target_duration = getTargetDuration();

    if ( start_time.is_not_a_date_time() == false ) {
        TimeDuration s_dtime= end_time - start_time;
        delta = target_duration - s_dtime;
        delta /= 4;
        LOG_TRACE( null_sink_i_base, " s_time/t_dime " << s_dtime << "/" << target_duration << "\n"  <<
                  " delta " << delta );
    }

    return delta;
}

/**
  DATA ANALYZER TEMPLATE Service Function for GR_BLOCK PATTERN
*/

template < typename IN_PORT_TYPE > int null_sink_i_base::_analyzerServiceFunction( typename  std::vector< gr_istream< IN_PORT_TYPE > > &istreams )
{
    typedef typename std::vector< gr_istream< IN_PORT_TYPE > > _IStreamList;

    boost::mutex::scoped_lock lock(serviceThreadLock);

    if ( validGRBlock() == false ) {
        // create our processing block
        createBlock();

        LOG_DEBUG( null_sink_i_base, " FINISHED BUILDING  GNU RADIO BLOCK");
    }
   
    // process any Stream ID changes this could affect number of io streams
    processStreamIdChanges();
    
    if ( !validGRBlock() || istreams.size() == 0 ) {
        LOG_WARN( null_sink_i_base, "NO STREAMS ATTACHED TO BLOCK..." );
        return NOOP;
    }

    // resize data vectors for passing data to GR_BLOCK object
    _input_ready.resize( istreams.size() );
    _ninput_items_required.resize( istreams.size());
    _ninput_items.resize( istreams.size());
    _input_items.resize(istreams.size());
    _output_items.resize(0);
  
    //
    // RESOLVE: need to look at forecast strategy, 
    //    1)  see how many read items are necessary for N number of outputs
    //    2)  read input data and see how much output we can produce
    //
  
    //
    // Grab available data from input streams
    //
    typename _IStreamList::iterator istream = istreams.begin();
    int nitems=0;
    for ( int idx=0 ; istream != istreams.end() && serviceThread->threadRunning() ; idx++, istream++ ) {
        // note this a blocking read that can cause deadlocks
        nitems = istream->read();

        if ( istream->overrun() ) {
            LOG_WARN( null_sink_i_base, " NOT KEEPING UP WITH STREAM ID:" << istream->streamID );
        }
    
        // RESOLVE issue when SRI changes that could affect the GNU Radio BLOCK
        if ( istream->sriChanged() ) {
            LOG_DEBUG( null_sink_i_base, "SRI CHANGED, STREAMD IDX/ID: " 
                   << idx << "/" << istream->pkt->streamID );
        }
    }

    LOG_TRACE( null_sink_i_base, "READ NITEMS: "  << nitems );
    if ( nitems <= 0 && !_istreams[0].eos() ) return NOOP;

    bool eos = false;
    int  nout = 0;
    while ( nout > -1 && serviceThread->threadRunning() ) {
        eos = false;
        nout = _forecastAndProcess( eos, istreams );
        if ( nout > -1  ) {
            // we chunked on data so move read pointer..
            istream = istreams.begin();
            for ( ; istream != istreams.end(); istream++ ) {

                int idx=std::distance( istreams.begin(), istream );
                // if we processed data for this stream
                if ( _input_ready[idx] ) {
                    size_t nitems = 0;
                    try {
                        nitems = gr_sptr->nitems_read( idx );
                    } catch(...){}
      
                    if ( nitems > istream->nitems() ) {
                        LOG_WARN( null_sink_i_base,  "WORK CONSUMED MORE DATA THAN AVAILABLE,  READ/AVAILABLE " 
                                 << nitems << "/" << istream->nitems() );
                        nitems = istream->nitems();
                    }
                    istream->consume( nitems );
                    LOG_TRACE( null_sink_i_base, " CONSUME READ DATA  ITEMS/REMAIN " << nitems << "/" << istream->nitems());
                }
            }
            gr_sptr->reset_read_index();
        }

        // check for not enough data return
        if ( nout == -1 ) {

            // check for  end of stream
            istream = istreams.begin();
            for ( ; istream != istreams.end() ; istream++) {
                if ( istream->eos() ) {
                    eos=true;
                }
            }

            if ( eos ) {
                LOG_TRACE( null_sink_i_base, " DATA NOT READY, EOS:" << eos );
                _forecastAndProcess( eos, istreams );
            }
        }
    }

    if ( eos ) {
        istream = istreams.begin();
        for ( ; istream != istreams.end() ; istream++) {
            int idx=std::distance( istreams.begin(), istream );
            LOG_TRACE( null_sink_i_base, " CLOSING INPUT STREAM IDX:" << idx );
            istream->close();
        }
    }

    //
    // set the read pointers of the GNU Radio Block to start at the beginning of the 
    // supplied buffers
    //
    gr_sptr->reset_read_index();

    LOG_TRACE( null_sink_i_base, " END OF ANALYZER SERVICE FUNCTION....." << noutput_items );

    if ( nout == -1 && eos == false ) {
        return NOOP; 
    } else {
        return NORMAL;
    }
}

template <  typename IN_PORT_TYPE > int null_sink_i_base::_forecastAndProcess( bool &eos, typename  std::vector< gr_istream< IN_PORT_TYPE > > &istreams )
{
    typedef typename std::vector< gr_istream< IN_PORT_TYPE > >   _IStreamList;

    typename _IStreamList::iterator istream = istreams.begin();
    int nout = 0;
    bool dataReady = false;
    if ( !eos ) {
        uint64_t max_items_avail = 0;
        for ( int idx=0 ; istream != istreams.end() && serviceThread->threadRunning() ; idx++, istream++ ) {
            LOG_TRACE( null_sink_i_base, "GET MAX ITEMS: STREAM:" << idx << " NITEMS/SCALARS:"
                      << istream->nitems() << "/" << istream->_data.size() );
            max_items_avail = std::max( istream->nitems(), max_items_avail );
        }

        //
        // calc number of output items to produce
        //
        noutput_items = (int) (max_items_avail * gr_sptr->relative_rate ());
        noutput_items = round_down (noutput_items, gr_sptr->output_multiple ());

        if ( noutput_items <= 0  ) {
           LOG_TRACE( null_sink_i_base, "DATA CHECK - MAX ITEMS  NOUTPUT/MAX_ITEMS:" <<   noutput_items << "/" << max_items_avail);
           return -1;
        }

        if ( gr_sptr->fixed_rate() ) {
            istream = istreams.begin();
            for ( int i=0; istream != istreams.end(); i++, istream++ ) {
                int t_noutput_items = gr_sptr->fixed_rate_ninput_to_noutput( istream->nitems() );
                if ( gr_sptr->output_multiple_set() ) {
                    t_noutput_items = round_up(t_noutput_items, gr_sptr->output_multiple());
                }
                if ( t_noutput_items > 0 ) {
                    if ( noutput_items == 0 ) {
                        noutput_items = t_noutput_items;
                    }
                    if ( t_noutput_items <= noutput_items ) {
                        noutput_items = t_noutput_items;
                    }
                }
            }
            LOG_TRACE( null_sink_i_base, " FIXED FORECAST NOUTPUT/output_multiple == " 
                      << noutput_items  << "/" << gr_sptr->output_multiple());
        }

        //
        // ask the block how much input they need to produce noutput_items...
        // if enough data is available to process then set the dataReady flag
        //
        int32_t  outMultiple = gr_sptr->output_multiple();
        while ( !dataReady && noutput_items >= outMultiple  ) {
            //
            // ask the block how much input they need to produce noutput_items...
            //
            gr_sptr->forecast(noutput_items, _ninput_items_required);

            LOG_TRACE( null_sink_i_base, "--> FORECAST IN/OUT " << _ninput_items_required[0]  << "/" << noutput_items  );

            istream = istreams.begin();
            uint32_t dr_cnt=0;
            for ( int idx=0 ; noutput_items > 0 && istream != istreams.end(); idx++, istream++ ) {
                // check if buffer has enough elements
                _input_ready[idx] = false;
                if ( istream->nitems() >= (uint64_t)_ninput_items_required[idx] ) {
                    _input_ready[idx] = true;
                    dr_cnt++;
                }
                LOG_TRACE( null_sink_i_base, "ISTREAM DATACHECK NELMS/NITEMS/REQ/READY:" << 
                          istream->nelems() << "/" << istream->nitems() << "/" << 
                          _ninput_items_required[idx] << "/" << _input_ready[idx]);
            }
    
            if ( dr_cnt < istreams.size() ) {
                if ( outMultiple > 1 ) {
                    noutput_items -= outMultiple;
                } else {
                    noutput_items /= 2;
                }
            } else {
                dataReady = true;
            }
            LOG_TRACE( null_sink_i_base, " TRIM FORECAST NOUTPUT/READY " << noutput_items << "/" << dataReady );
        }

        // check if data is ready...
        if ( !dataReady ) {
            LOG_TRACE( null_sink_i_base, "DATA CHECK - NOT ENOUGH DATA  AVAIL/REQ:" 
                      <<   _istreams[0].nitems() << "/" << _ninput_items_required[0] );
            return -1;
        }

        // reset looping variables
        int  ritems = 0;
        int  nitems = 0;

        // reset caching vectors
        _output_items.clear();
        _input_items.clear();
        _ninput_items.clear();
        istream = istreams.begin();

        for ( int idx=0 ; istream != istreams.end(); idx++, istream++ ) {
            // check if the stream is ready
            if ( !_input_ready[idx] ) continue;
      
            // get number of items remaining
            try {
                ritems = gr_sptr->nitems_read( idx );
            } catch(...){
                // something bad has happened, we are missing an input stream
                LOG_ERROR( null_sink_i_base, "MISSING INPUT STREAM FOR GR BLOCK, STREAM ID:" <<   istream->streamID );
                return -2;
            } 

            nitems = istream->nitems() - ritems;
            LOG_TRACE( null_sink_i_base,  " ISTREAM: IDX:" << idx  << " ITEMS AVAIL/READ/REQ " << nitems << "/" 
                      << ritems << "/" << _ninput_items_required[idx] );
            if ( nitems >= _ninput_items_required[idx] && nitems > 0 ) {
                //remove eos checks ...if ( nitems < _ninput_items_required[idx] ) nitems=0;
                _ninput_items.push_back( nitems );
                _input_items.push_back( (const void *) (istream->read_pointer(ritems)) );
            }
        }

        nout=0;
        if ( _input_items.size() != 0 && serviceThread->threadRunning() ) {
            LOG_TRACE( null_sink_i_base, " CALLING WORK.....N_OUT:" << noutput_items << 
                      " N_IN:" << nitems << " ISTREAMS:" << _input_items.size() << 
                      " OSTREAMS:" << _output_items.size());
            nout = gr_sptr->general_work( noutput_items, _ninput_items, _input_items, _output_items);

            // sink/analyzer patterns do not return items, so consume_each is not called in Gnu Radio BLOCK
            if ( nout == 0 ) {
                gr_sptr->consume_each(nitems);
            }

            LOG_TRACE( null_sink_i_base, "RETURN  WORK ..... N_OUT:" << nout);
        }

        // check for stop condition from work method
        if ( nout < gr_block::WORK_DONE ) {
            LOG_WARN( null_sink_i_base, "WORK RETURNED STOP CONDITION..." << nout );
            nout=0;
            eos = true;
        }
    }

    return nout;
 
}


