
doTest = function( signal ) {

    // Test replica set step down

    // Replica set testing API
    // Create a new replica set test. Specify set name and the number of nodes you want.
    var replTest = new ReplSetTest( {name: 'testSet', nodes: 3} );

    // call startSet() to start each mongod in the replica set
    // this returns a list of nodes
    var nodes = replTest.startSet();

    // Call initiate() to send the replSetInitiate command
    // This will wait for initiation
    replTest.initiate();

    // Get master node
    var master = replTest.getMaster();

    // Write some data to master
    // NOTE: this test fails unless we write some data.
    master.getDB("foo").foo.save({a: 1});
    master.getDB("foo").runCommand({getlasterror: 1, w:3, wtimeout: 20000});

    // Step down master
    master.getDB("admin").runCommand({replSetStepDown: true});

    try {
      var new_master = replTest.getMaster();
    }
    catch( err ) {
      throw( "Could not elect new master before timeout." );
    }

    assert( master != new_master, "Old master shouldn't be equal to new master." );

    // Make sure that slaves are still up
    var result = new_master.getDB("admin").runCommand({replSetGetStatus: true});

    assert( result['ok'] == 1, "Could not verify that slaves were still up:" + result );

    replTest.stopSet( 15 );
}

doTest( 15 );
