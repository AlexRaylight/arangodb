<a name="joins"></a>
# Joins

So far we have only dealt with one collection (`users`) at a time. We also have a 
collection `relations` that stores relationships between users. We will now use
this extra collection to create a result from two collections.

First of all, we'll query a few users together with their friends' ids. For that,
we'll use all `relations` that have a value of `friend` in their `type` attribute.
Relationships are established by using the `from` and `to` attributes in the 
`relations` collection, which point to the `id` values in the `users` collection.

<a name="join_tuples"></a>
### Join tuples

We'll start with a SQL-ish result set and return each tuple (user name, friend id) 
separately. The AQL query to generate such result is:

    FOR u IN users 
      FILTER u.active == true 
      LIMIT 0, 4 
      FOR f IN relations 
        FILTER f.type == "friend" && f.from == u.id 
        RETURN { 
          "user" : u.name, 
          "friendId" : f.to 
        }

    [ 
      { 
        "user" : "Abigail", 
        "friendId" : 108 
      }, 
      { 
        "user" : "Abigail", 
        "friendId" : 102 
      }, 
      { 
        "user" : "Abigail", 
        "friendId" : 106 
      }, 
      { 
        "user" : "Fred", 
        "friendId" : 209 
      }, 
      { 
        "user" : "Mary", 
        "friendId" : 207 
      }, 
      { 
        "user" : "Mary", 
        "friendId" : 104 
      }, 
      { 
        "user" : "Mariah", 
        "friendId" : 203 
      }, 
      { 
        "user" : "Mariah", 
        "friendId" : 205 
      } 
    ]

<a name="horizontal_lists"></a>
### Horizontal lists


Note that in the above result, a user might be returned multiple times. This is the
SQL way of returning data. If this is not desired, the friends' ids of each user
can be returned in a horizontal list. This will return each user at most once.

The AQL query for doing so is:

    FOR u IN users 
      FILTER u.active == true LIMIT 0, 4 
      RETURN { 
        "user" : u.name, 
        "friendIds" : (
          FOR f IN relations 
            FILTER f.from == u.id && f.type == "friend"
            RETURN f.to
        )
      }

    [ 
      { 
        "user" : "Abigail", 
        "friendIds" : [ 
          108, 
          102, 
          106 
        ] 
      }, 
      { 
        "user" : "Fred", 
        "friendIds" : [ 
          209 
        ] 
      }, 
      { 
        "user" : "Mary", 
        "friendIds" : [ 
          207, 
          104 
        ] 
      }, 
      { 
        "user" : "Mariah", 
        "friendIds" : [ 
          203, 
          205 
        ] 
      } 
    ]

In this query we are still iterating over the users in the `users` collection 
and for each matching user we are executing a sub-query to create the matching
list of related users.

<a name="self_joins"></a>
### Self joins


To not only return friend ids but also the names of friends, we could "join" the
`users` collection once more (something like a "self join"):
   
    FOR u IN users 
      FILTER u.active == true 
      LIMIT 0, 4 
      RETURN { 
        "user" : u.name, 
        "friendIds" : (
          FOR f IN relations 
            FILTER f.from == u.id && f.type == "friend" 
            FOR u2 IN users 
              FILTER f.to == u2.id 
              RETURN u2.name
        ) 
      }    

    [ 
      { 
        "user" : "Abigail", 
        "friendIds" : [ 
          "Jim", 
          "Jacob", 
          "Daniel" 
        ] 
      }, 
      { 
        "user" : "Fred", 
        "friendIds" : [ 
          "Mariah" 
        ] 
      }, 
      { 
        "user" : "Mary", 
        "friendIds" : [ 
          "Isabella", 
          "Michael" 
        ] 
      }, 
      { 
        "user" : "Mariah", 
        "friendIds" : [ 
          "Madison", 
          "Eva" 
        ] 
      } 
    ]

