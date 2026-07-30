const firebaseConfig = {
   apiKey: 'AIzaSyBwxTWUfwycAq3RBTGCkiYB5hSuGv-Dd4c',
   authDomain: 'absen-ab028.firebaseapp.com',
   databaseURL: 'https://absen-ab028-default-rtdb.firebaseio.com',
   projectId: 'absen-ab028',
   storageBucket: 'absen-ab028.firebasestorage.app',
   messagingSenderId: '1075341334790',
   appId: '1:1075341334790:web:5d68d724150c03e4c06687',
   measurementId: 'G-BS8BGLFMWK',
};

// Initialize Firebase
firebase.initializeApp(firebaseConfig);
const auth = firebase.auth();
const db = firebase.database();
