# Introduction #

This page describes our motivation for doing this project, as well as the approach we use.

## Motivation ##
We have manually ported some applications from other multiprogramming systems to the Cell BE. During this work, we became aware of the relative difficulty of this task. We also noted that applications with low memory usage were much easier to port than those with heavy memory usage.

We have considered many interresting projects for the Cell BE, which will ease the programming tasks. We consider the optimal scenario to be one where an application written for multihtreading programming, can run without modification on the Cell BE. Once this works, the programmer may make optimizations to the code. We consider this project to be the first step in that direction. The project itself is also interresting, and will reduce the amount of manual work, required to port an application to the Cell BE. Hopefully without too much performance loss.

Regardless of the approach to simplifying Cell BE programming, we quickly discovered that memory control was a problem. We therefore agreed that there is a need for a better memory model for the Cell BE. We find it very unlikely that extra hardware will be added to the Cell BE, and thus our approach is purely software based.

We have chosen to view a single Cell BE system as a distributed system, because this fits the actual architecture which has seperated units. We have researched existing hardware and software solutions for distributed systems, and noted what design criteria those systems use. The analysis of the systems is not avalible online, we have a danish version, if someone is interrested.